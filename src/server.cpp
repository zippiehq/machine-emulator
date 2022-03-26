// Copyright 2019 Cartesi Pte. Ltd.
//
// This file is part of the machine-emulator. The machine-emulator is free
// software: you can redistribute it and/or modify it under the terms of the GNU
// Lesser General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// The machine-emulator is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the machine-emulator. If not, see http://www.gnu.org/licenses/.
//

#include <string>
#include <cstdint>
#include <exception>

#define SERVER_VERSION_MAJOR UINT32_C(0)
#define SERVER_VERSION_MINOR UINT32_C(3)
#define SERVER_VERSION_PATCH UINT32_C(0)
#define SERVER_VERSION_PRE_RELEASE ""
#define SERVER_VERSION_BUILD ""

#include <signal.h>
#include <sys/wait.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <grpc++/grpc++.h>
#include <grpc++/resource_quota.h>
#include "cartesi-machine.grpc.pb.h"
#pragma GCC diagnostic pop

#include "grpc-util.h"
#include "machine.h"
#include "unique-c-ptr.h"

using namespace cartesi;
using hash_type = keccak_256_hasher::hash_type;
using namespace CartesiMachine;
using namespace Versioning;
using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

struct handler_context {
    std::unique_ptr<machine> m;
    Machine::AsyncService s;
    std::unique_ptr<ServerCompletionQueue> cq;
    bool ok;
    bool forked;
};

class i_handler {
public:

    enum class side_effect {
        none,
        snapshot,
        rollback,
        shutdown
    };

    side_effect advance(handler_context &hctx) {
        return do_advance(hctx);
    }

    virtual ~i_handler(void) {
        ;
    }

private:
    virtual side_effect do_advance(handler_context &hctx) = 0;

};

template <typename REQUEST, typename RESPONSE>
class handler: public i_handler {

    using sctx = ServerContext;
    using writer = ServerAsyncResponseWriter<RESPONSE>;

    writer m_writer;
    bool m_waiting;

    REQUEST m_request;
    sctx m_sctx;

    void renew_ctx(void) {
        m_writer.~writer();
        m_sctx.~sctx();
        new (&m_sctx) sctx();
        new (&m_writer) writer(&m_sctx);
    }

    side_effect do_advance(handler_context &hctx) override {
        if (m_waiting) {
            m_waiting = false;
            if (hctx.ok) {
                try {
                    return go(hctx, &m_request, &m_writer);
                } catch (std::exception& e) {
                    return finish_with_exception(&m_writer, e);
                }
            }
            return side_effect::none;
        } else {
            renew_ctx();
            m_waiting = true;
            return prepare(hctx, &m_sctx, &m_request, &m_writer);
        }
    }

    virtual side_effect prepare(
        handler_context &hctx,
        ServerContext *sctx,
        REQUEST *req,
        ServerAsyncResponseWriter<RESPONSE> *writer
    ) = 0;

    virtual side_effect go(
        handler_context &hctx,
        REQUEST *req,
        ServerAsyncResponseWriter<RESPONSE> *writer
    ) = 0;

protected:

    side_effect finish_ok(ServerAsyncResponseWriter<RESPONSE> *writer,
        const RESPONSE &resp, side_effect se = side_effect::none) {
        writer->Finish(resp, Status::OK, this);
        return se;
    }

    side_effect finish_with_error(ServerAsyncResponseWriter<RESPONSE> *writer,
        StatusCode sc, const char *e, side_effect se = side_effect::none) {
        writer->FinishWithError(Status(sc, e), this);
        return se;
    }

    side_effect finish_with_exception(
        ServerAsyncResponseWriter<RESPONSE> *writer,
        const std::exception& e, side_effect se = side_effect::none) {
        return finish_with_error(writer, StatusCode::ABORTED, e.what(), se);
    }

    side_effect finish_with_error_no_machine(
        ServerAsyncResponseWriter<RESPONSE> *writer) {
        return finish_with_error(writer, StatusCode::FAILED_PRECONDITION,
            "no machine", side_effect::none);
    }

public:

    virtual ~handler() {
        ;
    }

    handler(void): m_writer(&m_sctx), m_waiting(false) {
        ;
    }

};

static void squash_parent(bool &forked) {
    // If we are a forked child, we have a parent waiting.
    // We want to take its place before exiting.
    // Wake parent up by signaling ourselves to stop.
    // Parent will wake us back up and then exit.
    if (forked) {
        raise(SIGSTOP);
        // When we wake up, we took the parent's place, so we are not "forked" anymore
        forked = false;
    }
}

static void snapshot(bool &forked) {
    pid_t childid = 0;
    squash_parent(forked);
    // Now actually fork
    if ((childid = fork()) == 0) {
        // Child simply goes on with next loop iteration.
        forked = true;
    } else {
        // Parent waits on child.
        int wstatus;
        waitpid(childid, &wstatus, WUNTRACED);
        if (WIFSTOPPED(wstatus)) {
            // Here the child wants to take our place.
            // Wake child and exit.
            kill(childid, SIGCONT);
            exit(0);
        } else {
            // Here the child exited.
            // We take its place, but are not "forked" anymore.
            // We go on with next loop iteration.
            forked = false;
        }
    }
}

static void rollback(bool &forked) {
    if (forked) {
        // Here, we are a child and forked.
        // We simply exit so parent can take our place.
        exit(0);
    }
}

using machine_ptr = std::unique_ptr<machine>;

class handler_GetVersion final: public handler<Void, GetVersionResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<GetVersionResponse> *writer) override {
        hctx.s.RequestGetVersion(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<GetVersionResponse> *writer) override {
        (void) hctx;
        (void) req;
        GetVersionResponse resp;
        auto version = resp.mutable_version();
        version->set_major(SERVER_VERSION_MAJOR);
        version->set_minor(SERVER_VERSION_MINOR);
        version->set_patch(SERVER_VERSION_PATCH);
        version->set_pre_release(SERVER_VERSION_PRE_RELEASE);
        version->set_build(SERVER_VERSION_BUILD);
        return finish_ok(writer, resp);
    }

public:

    handler_GetVersion(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Machine final: public handler<MachineRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, MachineRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestMachine(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, MachineRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        if (hctx.m) {
            return finish_with_error(writer, StatusCode::FAILED_PRECONDITION,
                "machine already exists");
        }
        Void resp;
        switch (req->machine_oneof_case()) {
            case MachineRequest::kConfig:
                hctx.m = std::make_unique<cartesi::machine>(
                    get_proto_machine_config(req->config()));
                return finish_ok(writer, resp);
            case MachineRequest::kDirectory:
                hctx.m = std::make_unique<cartesi::machine>(req->directory());
                return finish_ok(writer, resp);
            default:
                return finish_with_error(writer, StatusCode::INVALID_ARGUMENT,
                    "invalid machine specification");
        }
    }

public:

    handler_Machine(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Run final: public handler<RunRequest, RunResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, RunRequest *req, ServerAsyncResponseWriter<RunResponse> *writer) override {
        hctx.s.RequestRun(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, RunRequest *req, ServerAsyncResponseWriter<RunResponse> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        uint64_t limit = (uint64_t) req->limit();
        if (limit < hctx.m->read_mcycle()) {
            return finish_with_error(writer, StatusCode::INVALID_ARGUMENT,
                "mcycle is past");
        }
        RunResponse resp;
        hctx.m->run(limit);
        resp.set_mcycle(hctx.m->read_mcycle());
        resp.set_tohost(hctx.m->read_htif_tohost());
        resp.set_iflags_h(hctx.m->read_iflags_H());
        resp.set_iflags_y(hctx.m->read_iflags_Y());
        return finish_ok(writer, resp);
    }

public:

    handler_Run(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Store final: public handler<StoreRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, StoreRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestStore(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, StoreRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        Void resp;
        hctx.m->store(req->directory());
        return finish_ok(writer, resp);
    }

public:

    handler_Store(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Destroy final: public handler<Void, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestDestroy(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) req;
        squash_parent(hctx.forked);
        if (hctx.m) {
            hctx.m.reset();
        }
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_Destroy(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Snapshot final: public handler<Void, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestSnapshot(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::snapshot;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) hctx;
        (void) req;
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_Snapshot(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Rollback final: public handler<Void, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestRollback(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::rollback;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) hctx;
        (void) req;
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_Rollback(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Shutdown final: public handler<Void, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestShutdown(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::shutdown;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) hctx;
        (void) req;
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_Shutdown(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_Step final: public handler<StepRequest, StepResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, StepRequest *req, ServerAsyncResponseWriter<StepResponse> *writer) override {
        hctx.s.RequestStep(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, StepRequest *req, ServerAsyncResponseWriter<StepResponse> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        AccessLog proto_log;
        StepResponse resp;
        set_proto_access_log(hctx.m->step(get_proto_log_type(req->log_type()),
                req->one_based()), resp.mutable_log());
        return finish_ok(writer, resp);
    }

public:

    handler_Step(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ReadMemory final: public handler<ReadMemoryRequest, ReadMemoryResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, ReadMemoryRequest *req, ServerAsyncResponseWriter<ReadMemoryResponse> *writer) override {
        hctx.s.RequestReadMemory(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, ReadMemoryRequest *req, ServerAsyncResponseWriter<ReadMemoryResponse> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        uint64_t address = req->address();
        uint64_t length = req->length();
        auto data = cartesi::unique_calloc<unsigned char>(length);
        hctx.m->read_memory(address, data.get(), length);
        ReadMemoryResponse resp;
        resp.set_data(data.get(), length);
        return finish_ok(writer, resp);
    }

public:

    handler_ReadMemory(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_WriteMemory final: public handler<WriteMemoryRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, WriteMemoryRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestWriteMemory(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, WriteMemoryRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        uint64_t address = req->address();
        const auto &data = req->data();
        const bool is_path = req->is_path();
        Void resp;
        hctx.m->write_memory(address,
            reinterpret_cast<const unsigned char *>(data.data()), data.size(), is_path);
        return finish_ok(writer, resp);
    }

public:

    handler_WriteMemory(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ReadWord final: public handler<ReadWordRequest, ReadWordResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, ReadWordRequest *req, ServerAsyncResponseWriter<ReadWordResponse> *writer) override {
        hctx.s.RequestReadWord(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, ReadWordRequest *req, ServerAsyncResponseWriter<ReadWordResponse> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        uint64_t address = req->address();
        ReadWordResponse resp;
        uint64_t word = 0;
        resp.set_success(hctx.m->read_word(address, word));
        resp.set_value(word);
        return finish_ok(writer, resp);
    }

public:

    handler_ReadWord(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetRootHash final: public handler<Void, GetRootHashResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<GetRootHashResponse> *writer) override {
        hctx.s.RequestGetRootHash(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<GetRootHashResponse> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        hctx.m->update_merkle_tree();
        merkle_tree::hash_type rh;
        hctx.m->get_root_hash(rh);
        GetRootHashResponse resp;
        set_proto_hash(rh, resp.mutable_hash());
        return finish_ok(writer, resp);
    }

public:

    handler_GetRootHash(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetProof final: public handler<GetProofRequest, GetProofResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, GetProofRequest *req, ServerAsyncResponseWriter<GetProofResponse> *writer) override {
        hctx.s.RequestGetProof(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, GetProofRequest *req, ServerAsyncResponseWriter<GetProofResponse> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        uint64_t address = req->address();
        int log2_size = static_cast<int>(req->log2_size());
        merkle_tree::proof_type p{};
        if (!hctx.m->update_merkle_tree()) {
            throw std::runtime_error{"Merkle tree update failed"};
        }
        hctx.m->get_proof(address, log2_size, p);
        GetProofResponse resp;
        set_proto_proof(p, resp.mutable_proof());
        return finish_ok(writer, resp);
    }

public:

    handler_GetProof(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ReplaceFlashDrive final: public handler<ReplaceFlashDriveRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, ReplaceFlashDriveRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestReplaceFlashDrive(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, ReplaceFlashDriveRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        hctx.m->replace_flash_drive(get_proto_flash_drive_config(
                req->config()));
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_ReplaceFlashDrive(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetXAddress final: public handler<GetXAddressRequest, GetXAddressResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, GetXAddressRequest *req, ServerAsyncResponseWriter<GetXAddressResponse> *writer) override {
        hctx.s.RequestGetXAddress(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, GetXAddressRequest *req, ServerAsyncResponseWriter<GetXAddressResponse> *writer) override {
        (void) hctx;
        auto index = req->index();
        if (index >= X_REG_COUNT) {
            throw std::invalid_argument{"invalid register index"};
        }
        GetXAddressResponse resp;
        resp.set_address(cartesi::machine::get_x_address(index));
        return finish_ok(writer, resp);
    }

public:

    handler_GetXAddress(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ReadX final: public handler<ReadXRequest, ReadXResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, ReadXRequest *req, ServerAsyncResponseWriter<ReadXResponse> *writer) override {
        hctx.s.RequestReadX(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, ReadXRequest *req, ServerAsyncResponseWriter<ReadXResponse> *writer) override {
        auto index = req->index();
        if (index >= X_REG_COUNT) {
            throw std::invalid_argument{"invalid register index"};
        }
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        ReadXResponse resp;
        resp.set_value(hctx.m->read_x(index));
        return finish_ok(writer, resp);
    }

public:

    handler_ReadX(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_WriteX final: public handler<WriteXRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, WriteXRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestWriteX(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, WriteXRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        auto index = req->index();
        if (index >= X_REG_COUNT || index <= 0) { // x0 is read-only
            throw std::invalid_argument{"invalid register index"};
        }
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        hctx.m->write_x(index, req->value());
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_WriteX(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ResetIflagsY final: public handler<Void, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestResetIflagsY(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        hctx.m->reset_iflags_Y();
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_ResetIflagsY(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetDhdHAddress final: public handler<GetDhdHAddressRequest, GetDhdHAddressResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, GetDhdHAddressRequest *req, ServerAsyncResponseWriter<GetDhdHAddressResponse> *writer) override {
        hctx.s.RequestGetDhdHAddress(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, GetDhdHAddressRequest *req, ServerAsyncResponseWriter<GetDhdHAddressResponse> *writer) override {
        (void) hctx;
        auto index = req->index();
        if (index >= DHD_H_REG_COUNT) {
            throw std::invalid_argument{"invalid register index"};
        }
        GetDhdHAddressResponse resp;
        resp.set_address(cartesi::machine::get_dhd_h_address(index));
        return finish_ok(writer, resp);
    }

public:

    handler_GetDhdHAddress(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ReadDhdH final: public handler<ReadDhdHRequest, ReadDhdHResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, ReadDhdHRequest *req, ServerAsyncResponseWriter<ReadDhdHResponse> *writer) override {
        hctx.s.RequestReadDhdH(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, ReadDhdHRequest *req, ServerAsyncResponseWriter<ReadDhdHResponse> *writer) override {
        auto index = req->index();
        if (index >= DHD_H_REG_COUNT) {
            throw std::invalid_argument{"invalid register index"};
        }
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        ReadDhdHResponse resp;
        resp.set_value(hctx.m->read_dhd_h(index));
        return finish_ok(writer, resp);
    }

public:

    handler_ReadDhdH(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_WriteDhdH final: public handler<WriteDhdHRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, WriteDhdHRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestWriteDhdH(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, WriteDhdHRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        auto index = req->index();
        if (index >= DHD_H_REG_COUNT) {
            throw std::invalid_argument{"invalid register index"};
        }
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        Void resp;
        hctx.m->write_dhd_h(index, req->value());
        return finish_ok(writer, resp);
    }

public:

    handler_WriteDhdH(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetCsrAddress final: public handler<GetCsrAddressRequest, GetCsrAddressResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, GetCsrAddressRequest *req, ServerAsyncResponseWriter<GetCsrAddressResponse> *writer) override {
        hctx.s.RequestGetCsrAddress(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, GetCsrAddressRequest *req, ServerAsyncResponseWriter<GetCsrAddressResponse> *writer) override {
        (void) hctx;
        if (!CartesiMachine::Csr_IsValid(req->csr())) {
            throw std::invalid_argument{"invalid CSR"};
        }
        auto csr = static_cast<cartesi::machine::csr>(req->csr());
        GetCsrAddressResponse resp;
        resp.set_address(cartesi::machine::get_csr_address(csr));
        return finish_ok(writer, resp);
    }

public:

    handler_GetCsrAddress(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_ReadCsr final: public handler<ReadCsrRequest, ReadCsrResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, ReadCsrRequest *req, ServerAsyncResponseWriter<ReadCsrResponse> *writer) override {
        hctx.s.RequestReadCsr(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, ReadCsrRequest *req, ServerAsyncResponseWriter<ReadCsrResponse> *writer) override {
        if (!CartesiMachine::Csr_IsValid(req->csr())) {
            throw std::invalid_argument{"invalid CSR"};
        }
        auto csr = static_cast<cartesi::machine::csr>(req->csr());
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        ReadCsrResponse resp;
        resp.set_value(hctx.m->read_csr(csr));
        return finish_ok(writer, resp);
    }

public:

    handler_ReadCsr(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_WriteCsr final: public handler<WriteCsrRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, WriteCsrRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestWriteCsr(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, WriteCsrRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        if (!CartesiMachine::Csr_IsValid(req->csr())) {
            throw std::invalid_argument{"invalid CSR"};
        }
        auto csr = static_cast<cartesi::machine::csr>(req->csr());
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        Void resp;
        hctx.m->write_csr(csr, req->value());
        return finish_ok(writer, resp);
    }

public:

    handler_WriteCsr(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetInitialConfig final: public handler<Void, GetInitialConfigResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<GetInitialConfigResponse> *writer) override {
        hctx.s.RequestGetInitialConfig(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<GetInitialConfigResponse> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        GetInitialConfigResponse resp;
        set_proto_machine_config(hctx.m->get_initial_config(),
            resp.mutable_config());
        return finish_ok(writer, resp);
    }

public:

    handler_GetInitialConfig(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_VerifyMerkleTree final: public handler<Void, VerifyMerkleTreeResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<VerifyMerkleTreeResponse> *writer) override {
        hctx.s.RequestVerifyMerkleTree(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<VerifyMerkleTreeResponse> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        VerifyMerkleTreeResponse resp;
        resp.set_success(hctx.m->verify_merkle_tree());
        return finish_ok(writer, resp);
    }

public:

    handler_VerifyMerkleTree(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_UpdateMerkleTree final: public handler<Void, UpdateMerkleTreeResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<UpdateMerkleTreeResponse> *writer) override {
        hctx.s.RequestUpdateMerkleTree(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<UpdateMerkleTreeResponse> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        UpdateMerkleTreeResponse resp;
        resp.set_success(hctx.m->update_merkle_tree());
        return finish_ok(writer, resp);
    }

public:

    handler_UpdateMerkleTree(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_VerifyDirtyPageMaps final: public handler<Void, VerifyDirtyPageMapsResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<VerifyDirtyPageMapsResponse> *writer) override {
        hctx.s.RequestVerifyDirtyPageMaps(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<VerifyDirtyPageMapsResponse> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        VerifyDirtyPageMapsResponse resp;
        resp.set_success(hctx.m->verify_dirty_page_maps());
        return finish_ok(writer, resp);
    }

public:

    handler_VerifyDirtyPageMaps(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_DumpPmas final: public handler<Void, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestDumpPmas(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) req;
        if (!hctx.m) {
            return finish_with_error_no_machine(writer);
        }
        hctx.m->dump_pmas();
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_DumpPmas(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_GetDefaultConfig final: public handler<Void, GetDefaultConfigResponse> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, Void *req, ServerAsyncResponseWriter<GetDefaultConfigResponse> *writer) override {
        hctx.s.RequestGetDefaultConfig(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, Void *req, ServerAsyncResponseWriter<GetDefaultConfigResponse> *writer) override {
        (void) hctx;
        (void) req;
        GetDefaultConfigResponse resp;
        set_proto_machine_config(machine::get_default_config(),
            resp.mutable_config());
        return finish_ok(writer, resp);
    }

public:

    handler_GetDefaultConfig(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_VerifyAccessLog final: public handler<VerifyAccessLogRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, VerifyAccessLogRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestVerifyAccessLog(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, VerifyAccessLogRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) hctx;
        Void resp;
        machine::verify_access_log(
            get_proto_access_log(req->log()),
            get_proto_machine_runtime_config(req->runtime()),
            req->one_based());
        return finish_ok(writer, resp);
    }

public:

    handler_VerifyAccessLog(handler_context &hctx) {
        advance(hctx);
    }
};

class handler_VerifyStateTransition final: public handler<VerifyStateTransitionRequest, Void> {

    side_effect prepare(handler_context &hctx, ServerContext *sctx, VerifyStateTransitionRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        hctx.s.RequestVerifyStateTransition(sctx, req, writer, hctx.cq.get(), hctx.cq.get(), this);
        return side_effect::none;
    }

    side_effect go(handler_context &hctx, VerifyStateTransitionRequest *req, ServerAsyncResponseWriter<Void> *writer) override {
        (void) hctx;
        machine::verify_state_transition(
            get_proto_hash(req->root_hash_before()),
            get_proto_access_log(req->log()),
            get_proto_hash(req->root_hash_after()),
            get_proto_machine_runtime_config(req->runtime()),
            req->one_based());
        Void resp;
        return finish_ok(writer, resp);
    }

public:

    handler_VerifyStateTransition(handler_context &hctx) {
        advance(hctx);
    }
};

std::unique_ptr<Server> build_server(const std::string &address, handler_context &hctx) {
    ServerBuilder builder;
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&hctx.s);
    hctx.cq = builder.AddCompletionQueue();
    return builder.BuildAndStart();
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        std::cerr << "Usage:\n";
        std::cerr << "  " << argv[0] << " <address>\n";
        std::cerr << "where <address> can be\n";
        std::cerr << "  <ipv4-hostname/address>:<port>\n";
        std::cerr << "  <ipv6-hostname/address>:<port>\n";
        std::cerr << "  unix:<path>\n";
        exit(1);
    }

    const char *address = argv[1];
    handler_context hctx{};
    auto server = build_server(address, hctx);
    if (!server) {
        std::cerr << "server creation failed\n";
        exit(1);
    }

    handler_GetVersion hGetVersion(hctx);
    handler_Machine hMachine(hctx);
    handler_Run hRun(hctx);
    handler_Store hStore(hctx);
    handler_Destroy hDestroy(hctx);
    handler_Snapshot hSnapshot(hctx);
    handler_Rollback hRollback(hctx);
    handler_Shutdown hShutdown(hctx);
    handler_Step hStep(hctx);
    handler_ReadMemory hReadMemory(hctx);
    handler_WriteMemory hWriteMemory(hctx);
    handler_ReadWord hReadWord(hctx);
    handler_GetRootHash hGetRootHash(hctx);
    handler_GetProof hGetProof(hctx);
    handler_ReplaceFlashDrive hReplaceFlashDrive(hctx);
    handler_GetXAddress hGetXAddress(hctx);
    handler_ReadX hReadX(hctx);
    handler_WriteX hWriteX(hctx);
    handler_ResetIflagsY hResetIflagsY(hctx);
    handler_GetDhdHAddress hGetDhdHAddress(hctx);
    handler_ReadDhdH hReadDhdH(hctx);
    handler_WriteDhdH hWriteDhdH(hctx);
    handler_GetCsrAddress hGetCsrAddress(hctx);
    handler_ReadCsr hReadCsr(hctx);
    handler_WriteCsr hWriteCsr(hctx);
    handler_GetInitialConfig hGetInitialConfig(hctx);
    handler_VerifyMerkleTree hVerifyMerkleTree(hctx);
    handler_UpdateMerkleTree hUpdateMerkleTree(hctx);
    handler_VerifyDirtyPageMaps hVerifyDirtyPageMaps(hctx);
    handler_DumpPmas hDumpPmas(hctx);
    handler_GetDefaultConfig hGetDefaultConfig(hctx);
    handler_VerifyAccessLog hVerifyAccessLog(hctx);
    handler_VerifyStateTransition hVerifyStateTransition(hctx);

    // The invariant before and after snapshot/rollbacks is that all handlers
    // are in waiting mode
    for ( ;; ) {
        using side_effect = i_handler::side_effect;
        i_handler *h = nullptr;
        if (!hctx.cq->Next(reinterpret_cast<void **>(&h), &hctx.ok))
            goto shutdown;
        switch (h->advance(hctx)) {
            case side_effect::none:
                // do nothing
                break;
            case side_effect::snapshot:
                snapshot(hctx.forked);
                break;
            case side_effect::rollback:
                rollback(hctx.forked);
                break;
            case side_effect::shutdown:
                goto shutdown;
        }
    }

shutdown:
    // Shutdown server before completion queue
    server->Shutdown();
    hctx.cq->Shutdown();
    {
        // Drain completion queue before exiting
        bool ok = false;
        i_handler *h = nullptr;
        while (hctx.cq->Next(reinterpret_cast<void **>(&h), &ok));
    }
    // Make sure we don't leave a snapshot burried
    squash_parent(hctx.forked);
    return 0;
}

