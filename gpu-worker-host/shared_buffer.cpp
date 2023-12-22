//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "shared_buffer.h"

#include <getopt.h>
#include <stdlib.h>

#include <cerrno>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "edge/edge_common.h"
#include "host/keystone.h"
#include "verifier/report.h"

void
SharedBuffer::set_ok() {
  edge_call_->return_data.call_status = CALL_STATUS_OK;
}

void
SharedBuffer::set_bad_offset() {
  edge_call_->return_data.call_status = CALL_STATUS_BAD_OFFSET;
}
void
SharedBuffer::set_bad_ptr() {
  edge_call_->return_data.call_status = CALL_STATUS_BAD_PTR;
}

int
SharedBuffer::get_ptr_from_offset(edge_data_offset offset, uintptr_t* ptr) {
  /* Validate that _shared_start+offset is sane */
  if (offset > UINTPTR_MAX - buffer_ || offset > buffer_len_) {
    return -1;
  }

  /* ptr looks valid, create it */
  *ptr = buffer_ + offset;
  return 0;
}

int
SharedBuffer::args_ptr(uintptr_t* ptr, size_t* size) {
  *size = edge_call_->call_arg_size;
  return get_ptr_from_offset(edge_call_->call_arg_offset, ptr);
}

std::optional<std::pair<uintptr_t, size_t>>
SharedBuffer::get_call_args_ptr_or_set_bad_offset() {
  uintptr_t call_args;
  size_t arg_len;
  if (args_ptr(&call_args, &arg_len) != 0) {
    set_bad_offset();
    return std::nullopt;
  }
  return std::pair{call_args, arg_len};
}

std::optional<char*>
SharedBuffer::get_c_string_or_set_bad_offset() {
  auto v = get_call_args_ptr_or_set_bad_offset();
  return v.has_value() ? std::optional{(char*)v.value().first} : std::nullopt;
}

std::optional<unsigned long>
SharedBuffer::get_unsigned_long_or_set_bad_offset() {
  auto v = get_call_args_ptr_or_set_bad_offset();
  return v.has_value() ? std::optional{*(unsigned long*)v.value().first}
                       : std::nullopt;
}

//std::optional<Report>
//SharedBuffer::get_report_or_set_bad_offset() {
//  auto v = get_call_args_ptr_or_set_bad_offset();
//  if (!v.has_value()) return std::nullopt;
//  Report ret;
//  ret.fromBytes((byte*)v.value().first);
//  return ret;
//}

uintptr_t
SharedBuffer::data_ptr() {
  return (uintptr_t)edge_call_ + sizeof(struct edge_call);
}

int
SharedBuffer::validate_ptr(uintptr_t ptr) {
  /* Validate that ptr starts in range */
  if (ptr > buffer_ + buffer_len_ || ptr < buffer_) {
    return 1;
  }
  return 0;
}

int
SharedBuffer::get_offset_from_ptr(uintptr_t ptr, edge_data_offset* offset) {
  int valid = validate_ptr(ptr);
  if (valid != 0) return valid;

  /* ptr looks valid, create it */
  *offset = ptr - buffer_;
  return 0;
}

int
SharedBuffer::setup_ret(void* ptr, size_t size) {
  edge_call_->return_data.call_ret_size = size;
  return get_offset_from_ptr(
      (uintptr_t)ptr, &edge_call_->return_data.call_ret_offset);
}

void
SharedBuffer::setup_ret_or_bad_ptr(unsigned long ret_val) {
  // Assuming we are done with the data section for args, use as
  // return region.
  //
  // TODO safety check?
  uintptr_t data_section = data_ptr();

  memcpy((void*)data_section, &ret_val, sizeof(unsigned long));

  if (setup_ret((void*)data_section, sizeof(unsigned long))) {
    set_bad_ptr();
  } else {
    set_ok();
  }
}

int
SharedBuffer::setup_wrapped_ret(void* ptr, size_t size) {
  struct edge_data data_wrapper;
  data_wrapper.size = size;
  get_offset_from_ptr(
      buffer_ + sizeof(struct edge_call) + sizeof(struct edge_data),
      &data_wrapper.offset);

  memcpy(
      (void*)(buffer_ + sizeof(struct edge_call) + sizeof(struct edge_data)),
      ptr, size);

  memcpy(
      (void*)(buffer_ + sizeof(struct edge_call)), &data_wrapper,
      sizeof(struct edge_data));

  edge_call_->return_data.call_ret_size = sizeof(struct edge_data);
  return get_offset_from_ptr(
      buffer_ + sizeof(struct edge_call),
      &edge_call_->return_data.call_ret_offset);
}

void
SharedBuffer::setup_wrapped_ret_or_bad_ptr(const std::string& ret_val) {
  if (setup_wrapped_ret((void*)ret_val.c_str(), ret_val.length() + 1)) {
    set_bad_ptr();
  } else {
    set_ok();
  }
  return;
}

