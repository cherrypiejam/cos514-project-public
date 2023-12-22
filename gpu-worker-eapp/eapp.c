//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------

// #include "eapp_utils.h"
// #include "edge_call.h"

#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "edge/edge_common.h"
#include <stdarg.h>
// #include <stdlib.h>
#include <stdio.h>
#include "malloc.h"
#include <syscall.h>

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

// Static allocation, to avoid OOM errors when logging.
static char enclave_log_buf[2048];

void
enclave_log(const char *format, ...) {
  va_list args;
  int n;
  va_start(args, format);

  n = vsnprintf(&enclave_log_buf[0], sizeof(enclave_log_buf), format, args);

  // Even if we run out of space, null-terminate the string
  enclave_log_buf[(sizeof enclave_log_buf) - 1] = '\0';

  ocall(
    OCALLCMD_LOG_MSG,
    &enclave_log_buf[0],
    // Include null-terminator at n + 1, but cap at buffer length
    (n < (sizeof enclave_log_buf)) ? n + 1 : (sizeof enclave_log_buf),
    NULL,
    0
  );

  va_end(args);
}

void run_helloworld(){
  ocall(OCALLCMD_HELLOWORLD_PRINT_STRING, "Hello World", 12, NULL, 0);
}

void run_matmul(void);

int main(){
  unsigned long retval = OCALLRET_EV_LOOP;

  while (retval != OCALLRET_EXIT) {
    ocall(OCALLCMD_EV_LOOP, NULL, 0, &retval ,sizeof(unsigned long));

    switch (retval) {
      case OCALLRET_EXIT: // EXIT
      case OCALLRET_EV_LOOP: // EV_LOOP
	break;
      case OCALLRET_START_HELLOWORLD:
	run_helloworld();
	break;
      case OCALLRET_START_MATMUL:
	run_matmul();
	break;
    }
  }

  EAPP_RETURN(0);
}


/////////////////////////////
///  MAT MUL              ///                          
/////////////////////////////

typedef size_t checksum_state_t;

void checksum_init(checksum_state_t *state);
void checksum(checksum_state_t *state, void *input, size_t size);
size_t checksum_finalize(checksum_state_t *state);
size_t matrix_mul(float *m1, float *m2, float *m3, size_t *d1, size_t *d2);

void run_matmul() {
  checksum_state_t input_cs, output_cs;
  checksum_init(&input_cs);
  checksum_init(&output_cs);

  struct edge_data retdata;
  ocall(OCALLCMD_MATMUL_GET_MATRIX_DIMS, NULL, 0, &retdata, sizeof(struct edge_data));

  size_t matrix_dims[2];
  if (retdata.size != 2 * sizeof(size_t)) {
    enclave_log("Invalid matrix dimensions buffer size!\r\n");
    EAPP_RETURN(1);
  }
  copy_from_shared((uint8_t*) matrix_dims, retdata.offset, retdata.size);
  checksum(&input_cs, matrix_dims, retdata.size);
  enclave_log("Received matrix dimensions %lu x %lu, allocating...\r\n",
              matrix_dims[0], matrix_dims[1]);
  size_t matrix_size = matrix_dims[0] * matrix_dims[1];
  //float* matrix = malloc(matrix_size);
 
  float *m1 =  malloc(sizeof(float) * matrix_size);
  float *m2 =  malloc(sizeof(float) * matrix_size);
  float *m3 =  malloc(sizeof(float) * matrix_size);
  enclave_log("Allocated matrix buffer.\r\n");

  // Chunked copy:
  for (size_t i = 0; i < 2; i++) {
    size_t offset = 0;
    float* m = i == 0 ? m1 : i == 1 ? m2 : m3;
    while (offset < matrix_size) {
      enclave_log("Copying at offset %lu\r\n", offset);
      ocall(OCALLCMD_MATMUL_GET_MATRIX_IN, NULL, 0, &retdata, sizeof(struct edge_data));
      size_t copy_len = (matrix_size - offset < retdata.size) ? matrix_size - offset : retdata.size;
      copy_from_shared((uint8_t*) &m[offset], retdata.offset, copy_len);
      checksum(&input_cs, m, copy_len);
      offset += copy_len;
    }
    enclave_log("Matrix... DONE %lu\r\n", i);
  }

  size_t ret = matrix_mul(m1, m2, m3, matrix_dims, matrix_dims);
  enclave_log("Matrix MUL DONE %f\r\n", m3[0]);

  size_t sum = checksum_finalize(&input_cs);
  enclave_log("Input: checksumed! %lu\r\n", sum);

}


void checksum_init(checksum_state_t *state) {
  *state = 0;
  return;
}

void checksum(checksum_state_t *state, void *input, size_t size) {
    for (char *cur = input; cur <= (char *)input + size * sizeof(char); cur++)  {
        for (int i = 0; i < sizeof(char) * 8; i++) {
            *state += (*cur >> i) & 1;
        }
    }
    return;
}

size_t checksum_finalize(checksum_state_t *state) {
  return *state;
}


size_t matrix_mul(float *m1, float *m2, float *m3, size_t *dims1, size_t *dims2) {
    size_t dim1_r = dims1[0];
    size_t dim1_c = dims1[1];

    size_t dim2_r = dims2[0];
    size_t dim2_c = dims2[1];

    if (dim1_c != dim2_r) {
        return 1;
    }
    size_t a = dim1_c;

    for (int m1r = 0; m1r < dim1_r; m1r++) {
        for (int m2c = 0; m2c < dim2_c; m2c++) {
            float sum = 0;
            for (int i = 0; i < a; i++) {
                sum += m1[m1r * a + i] * m2[i * a + m2c];
            }
            m3[m1r * a + m2c] = sum;
        }
    }

    return 0;
}
