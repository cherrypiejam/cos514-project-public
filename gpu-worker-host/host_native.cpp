//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include <vector>
#include <edge_call.h>
#include <keystone.h>
#include <rpc/server.h>
#include <optional>
#include <thread>
#include <condition_variable>
#include <mutex>
#include "shared_buffer.h"

using namespace std::chrono_literals;

// misc
#define OCALLRET_EXIT    0
#define OCALLRET_EV_LOOP 1
#define OCALLCMD_EV_LOOP 1
#define OCALLCMD_LOG_MSG 2

// helloworld
#define OCALLRET_START_HELLOWORLD 2
#define OCALLCMD_HELLOWORLD_PRINT_STRING 100

// matmul
#define OCALLRET_START_MATMUL 3
#define OCALLCMD_MATMUL_GET_MATRIX_DIMS 100
#define OCALLCMD_MATMUL_GET_MATRIX_IN 101
#define OCALLCMD_MATMUL_COPY_REPORT 102


unsigned long
print_string(char* str);
void
print_string_wrapper(void* buffer);
#define OCALL_PRINT_STRING 1

/***
 * An example call that will be exposed to the enclave application as
 * an "ocall". This is performed by an edge_wrapper function (below,
 * print_string_wrapper) and by registering that wrapper with the
 * enclave object (below, main).
 ***/
unsigned long
print_string(char* str) {
  return printf("Enclave said: \"%s\"\n", str);
}

struct EnclaveWrapper {
public:
  EnclaveWrapper(std::vector<uint8_t> enclaveAppBinary, std::vector<uint8_t> runtimeBinary, std::vector<uint8_t> loader);
  EnclaveWrapper(const EnclaveWrapper&) = delete;
  EnclaveWrapper(const EnclaveWrapper&&) = delete;

  void registerCallDispatch(std::function<bool(SharedBuffer&)>);
  void waitCallDispatchDeregistered();
 
private: 
  Keystone::Enclave enclave;
  Keystone::Params params;
  std::vector<uint8_t> enclaveAppBinary, runtimeBinary, loaderBinary;
  Keystone::ElfFile *enclaveAppFile, *runtimeFile, *loaderFile;
  std::thread enclaveThread;
  std::optional<std::function<bool(SharedBuffer&)>> rpcCallDispatch;
  std::mutex rpcCallDispatchLock;
  std::condition_variable rpcCallDispatchCV;

  void incomingOcall(void* buffer);
};

EnclaveWrapper::EnclaveWrapper(std::vector<uint8_t> enclaveAppBinary, std::vector<uint8_t> runtimeBinary, std::vector<uint8_t> loaderBinary) : enclaveAppBinary(enclaveAppBinary), runtimeBinary(runtimeBinary), loaderBinary(loaderBinary) {
  enclaveThread = std::thread([this]() {
    this->params.setFreeMemSize(64 * 1024 * 1024);
    //this->params.setSimulated(true);
    this->params.setUntrustedMem(DEFAULT_UNTRUSTED_PTR, 1024 * 1024);
    
    void *enclaveAppAddr = this->enclaveAppBinary.data();
    //std::cout << "Pointer???? " << enclaveAppAddr << std::endl;
    std::vector<uint8_t> newLoaderBinary = this->loaderBinary;
    std::cout << "File pointers: " << (void*) this->enclaveAppBinary.data() << ", " << (void*) this->runtimeBinary.data() << ", " << (void*) this->loaderBinary.data() << std::endl;
    std::cout << "File pointers: " << (void*) this->enclaveAppBinary.data() << ", " << (void*) this->runtimeBinary.data() << ", " << (void*) newLoaderBinary.data() << std::endl;

    this->enclaveAppFile = new Keystone::ElfFile(this->enclaveAppBinary.data(), this->enclaveAppBinary.size());
    this->runtimeFile = new Keystone::ElfFile(this->runtimeBinary.data(), this->runtimeBinary.size());
    this->loaderFile = new Keystone::ElfFile(newLoaderBinary.data(), newLoaderBinary.size());

    std::cout << "Host: Initializing enclave..." << std::endl;
    enclave.init(this->enclaveAppFile, this->runtimeFile, this->loaderFile, this->params, (uintptr_t)0);

    std::cout << "Host: Initializing enclave..." << std::endl;
    enclave.registerOcallDispatch([this](void* buffer) {
      return this->incomingOcall(buffer);
    });

    std::cout << "Host: Initializing enclave..." << std::endl;
    /* We must specifically register functions we want to export to the
       enclave. */
    //register_call(OCALL_PRINT_STRING, print_string_wrapper);

    std::cout << "Host: Initializing enclave..." << std::endl;
    edge_call_init_internals(
        (uintptr_t)this->enclave.getSharedBuffer(), this->enclave.getSharedBufferSize());

    std::cout << "Host: Running enclave..." << std::endl;
    this->enclave.run();
    std::cout << "Host: Enclave finished!" << std::endl;
  });
}

// Runs in enclave thread
//
// if we have the rpcCallDispatch non-null, then call that,
// otherwise only handle the enclave sleep ocall and wait
// for condvar. Any other ocall while not in sleep is illegal
// and should crash.
void EnclaveWrapper::incomingOcall(void* buffer) {
    std::cout << "Host: Received enclave ocall" << std::endl;
    std::unique_lock<std::mutex> rpcCallDispatchLg(rpcCallDispatchLock);
    SharedBuffer shbuf(enclave.getSharedBuffer(), enclave.getSharedBufferSize());
    struct edge_call* edge_call = (struct edge_call*)shbuf.ptr();

    if (rpcCallDispatch.has_value()) {
      std::cout << "Host: Enclave event loop call, dispatching..." << std::endl;
      if (!(*rpcCallDispatch)(shbuf)) {
        rpcCallDispatch = std::nullopt;
        rpcCallDispatchCV.notify_all();
      }
    } else if (edge_call->call_id == 1) {
      std::cout << "Host: Enclave event loop, waiting for CV!" << std::endl;
      rpcCallDispatchCV.wait(rpcCallDispatchLg, [this]() { return this->rpcCallDispatch.has_value(); });
      std::cout << "Host: Got CV notify, returning to eapp!" << std::endl;
      shbuf.setup_ret_or_bad_ptr(1);
    } else {
      std::cout << "Host: Enclave made illegal ocall from main event loop!" << std::endl;
    }
}

// Runs in user thread
//void EnclaveWrapper::withCallDispatch(std::function<void(SharedBuffer&)> dispatchFn, std::function<void()> scopeFn) {
//    // 1. Set rpcCallDispatch mutex to dispatchFn
//    {
//      std::lock_guard<std::mutex> rpcCallDispatchLg(rpcCallDispatchLock);
//      rpcCallDispatch.emplace(dispatchFn);
//    }
//
//    // 2. Wake up enclave using condvar signal
//    rpcCallDispatchCV.notify_all();
//
//    // 3. Run scopeFn, wait for return
//    scopeFn();
//
//    // 4. Clear rpcCallDispatch
//    {
//      std::lock_guard<std::mutex> rpcCallDispatchLg(rpcCallDispatchLock);
//      rpcCallDispatch = std::nullopt;
//    }
//}
void EnclaveWrapper::registerCallDispatch(std::function<bool(SharedBuffer&)> dispatchFn) {
    // 1. Set rpcCallDispatch mutex to dispatchFn
    {
      std::lock_guard<std::mutex> rpcCallDispatchLg(rpcCallDispatchLock);
      rpcCallDispatch.emplace(dispatchFn);
    }

    // 2. Wake up enclave using condvar signal
    rpcCallDispatchCV.notify_all();
}

void EnclaveWrapper::waitCallDispatchDeregistered() {
  std::unique_lock<std::mutex> rpcCallDispatchLg(rpcCallDispatchLock);
  rpcCallDispatchCV.wait(rpcCallDispatchLg, [this]() { return !this->rpcCallDispatch.has_value(); });
}

int
main(int argc, char** argv) {
  // Creating a server that listens on port 8080
  rpc::server srv(5826);

  // Host application state
  std::mutex enclaveWrapperLock;
  std::optional<EnclaveWrapper> enclaveWrapper = std::nullopt;

  // Preinitialize the enclave parameters:

  srv.bind("eapp", [&enclaveWrapper, &enclaveWrapperLock](std::vector<uint8_t> enclaveApp, std::vector<uint8_t> runtime, std::vector<uint8_t> loader) {
    std::lock_guard<std::mutex> eclaveWrapperLg(enclaveWrapperLock);

    if (enclaveWrapper.has_value()) {
      return false;
    }

    enclaveWrapper.emplace(
      std::move(enclaveApp), std::move(runtime), std::move(loader));
    return true;
  });

  srv.bind("helloworld", [&enclaveWrapper, &enclaveWrapperLock]() {
    std::lock_guard<std::mutex> eclaveWrapperLg(enclaveWrapperLock);

    if (!enclaveWrapper.has_value()) {
      return false;
    }

    bool finished = false;

    (*enclaveWrapper).registerCallDispatch([&finished](SharedBuffer& shbuf) {
      struct edge_call* edge_call = (struct edge_call*)shbuf.ptr();
      std::cout << "Host: Got edge call id " << edge_call->call_id << std::endl;

      if (edge_call->call_id == OCALLCMD_EV_LOOP) {
        // Kick of "hello world" op
        shbuf.setup_ret_or_bad_ptr(OCALLRET_START_HELLOWORLD);
      } else if (edge_call->call_id == OCALLCMD_HELLOWORLD_PRINT_STRING) {
        // Print actual hello world message
        auto t = shbuf.get_c_string_or_set_bad_offset();
	if (t.has_value()) {
	  printf("Enclave said: %s", t.value());
	  auto ret_val = strlen(t.value());
	  shbuf.setup_ret_or_bad_ptr(ret_val);
	}

	finished = true;
      } else  {
        std::cout << "Host: Got spurious call!" << std::endl;
        shbuf.setup_ret_or_bad_ptr(OCALLRET_EXIT);
      }

      return !finished;
    });

    (*enclaveWrapper).waitCallDispatchDeregistered();

    return true;
  });


  srv.bind("matmul", [&enclaveWrapper, &enclaveWrapperLock]() {
    std::lock_guard<std::mutex> eclaveWrapperLg(enclaveWrapperLock);

    if (!enclaveWrapper.has_value()) {
      return false;
    }

    bool finished = false;

    (*enclaveWrapper).registerCallDispatch([&finished](SharedBuffer& shbuf) {
      struct edge_call* edge_call = (struct edge_call*)shbuf.ptr();
      std::cout << "Host: Got edge call id " << edge_call->call_id << std::endl;

      if (edge_call->call_id == OCALLCMD_EV_LOOP) {
        shbuf.setup_ret_or_bad_ptr(OCALLRET_START_MATMUL);
      } else if (edge_call->call_id == OCALLCMD_MATMUL_GET_MATRIX_DIMS) {
        size_t dims[2] = { 2, 2 };
        shbuf.setup_wrapped_ret(dims, sizeof(size_t) * 2);
      } else if (edge_call->call_id == OCALLCMD_MATMUL_GET_MATRIX_IN) {
        float in[4] = { 0.6, 1.0, 1.0, 1.0 };
        shbuf.setup_wrapped_ret(in, sizeof(float) * 4);
      } else if (edge_call->call_id == OCALLCMD_MATMUL_COPY_REPORT) {
        finished = true;
        shbuf.setup_ret_or_bad_ptr(0);
      } else {
        std::cout << "Host: Got spurious call!" << std::endl;
        shbuf.setup_ret_or_bad_ptr(OCALLRET_EXIT);
      }

      return !finished;
    });

    (*enclaveWrapper).waitCallDispatchDeregistered();

    return true;
  });

  std::cout << "Host: Listening for incoming RPC requests!" << std::endl;
  srv.run();

  return 0;
}

/***
 * Example edge-wrapper function. These are currently hand-written
 * wrappers, but will have autogeneration tools in the future.
 ***/
void
print_string_wrapper(void* buffer) {
  /* Parse and validate the incoming call data */
  struct edge_call* edge_call = (struct edge_call*)buffer;
  uintptr_t call_args;
  unsigned long ret_val;
  size_t arg_len;
  if (edge_call_args_ptr(edge_call, &call_args, &arg_len) != 0) {
    edge_call->return_data.call_status = CALL_STATUS_BAD_OFFSET;
    return;
  }

  /* Pass the arguments from the eapp to the exported ocall function */
  ret_val = print_string((char*)call_args);

  /* Setup return data from the ocall function */
  uintptr_t data_section = edge_call_data_ptr();
  memcpy((void*)data_section, &ret_val, sizeof(unsigned long));
  if (edge_call_setup_ret(
          edge_call, (void*)data_section, sizeof(unsigned long))) {
    edge_call->return_data.call_status = CALL_STATUS_BAD_PTR;
  } else {
    edge_call->return_data.call_status = CALL_STATUS_OK;
  }

  /* This will now eventually return control to the enclave */
  return;
}
