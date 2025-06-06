/* Copyright 2024-2025, Alejandro A. García <aag@zorzal.net>
 * SPDX-License-Identifier: MIT
 *
 * Machine learning blocks of operations.
 */
#pragma once
#include "ccommon/vector.h"
#include "ccommon/stream.h"
#include "ccommon/logging.h"
#include "ccommon/stringstore.h"
#include "ccompute/tensorstore.h"
#include "localtensor.h"

#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml_extend.h"

//TODO: load: if CPU backend, do not copy tensor data
//TODO: option: free compute, keep params in memory

typedef struct ggml_tensor MLTensor;

enum {
	MLB_NAME_BLOCK_BEGIN	= -0x1000,
	MLB_NAME_SPLIT			= -0x1001,
};

enum MLCtxFlags {
	// Prepare the computation to allow multiple calls to mlctx_compute
	MLB_F_MULTI_COMPUTE = 1,
	// Do produce any information output
	MLB_F_QUIET			= 2,
	//(debug) Dump the computation graph to a file
	MLB_F_DUMP			= 4,
};

typedef struct {
	MLTensor *tensor;
	StringInt name,
	          key;  //Full name to load from the tensor store
} MLCtxTensor;

typedef struct {
	ggml_backend_t backend;  //Fill
	TensorStore *tstore;  //Fill
	StringStore *ss;  //Tensor names are stored here
	
	struct ggml_context *cp, *cc; //params, compute
	struct ggml_cgraph *graph;
    ggml_gallocr_t allocr;

#if USE_GGML_SCHED
	ggml_backend_t backend2;  //Fill
	ggml_backend_sched_t sched;
	ggml_backend_buffer_t bkbuf;
#endif
	
	MLCtxTensor * tensors;  //vector
	MLTensor ** inputs;  //vector
	MLTensor * result;

	// Configuration
	struct {
		enum ggml_type wtype;  //weights type (default F16)
		unsigned n_tensor_max;
		char tpath_sep;  //default: '.'
		const char *tprefix;  //Tensor names prefix
		const char *name;  //Computation name, set by mlctx_begin
		int flags;  //MLB_F_*
		int flags_e;  //Flags valid until the next mlctx_begin
	} c;

	// Information/statistics
	struct MLCtxInfo {
		size_t mem_params, mem_compute, mem_total;
		double t_load, t_compute;
		unsigned n_compute, n_conv;
	} info;
} MLCtx;

void mlctx_free(MLCtx* C);

void mlctx_begin(MLCtx* C, const char* name);

void mlctx_end(MLCtx* C);

// All in one
int mlctx_run_(MLCtx* C, LocalTensor* out, const LocalTensor** inputs);
#define mlctx_run(C,O,...) \
	mlctx_run_((C), (O), (const LocalTensor*[]){ __VA_ARGS__, NULL })

// Build, alloc and load
// Pending: set input, compute, get output, free
int mlctx_prep(MLCtx* C);

/* Step by step interface */

// No need to call build
void mlctx_block_graph_dump(const MLCtx* C, Stream* out);
int mlctx_block_graph_dump_path(const MLCtx* C, const char* path);

int mlctx_build_alloc(MLCtx* C, MLTensor* result);

int mlctx_tstore_load(MLCtx* C, TensorStore* ts);

int mlctx_compute(MLCtx* C);

/* aux */

int tstore_tensor_read(TSTensorEntry*, struct ggml_tensor*);

/* Functions to define blocks */

static inline
void mlctx_block_begin(MLCtx* C)
{
	vec_push(C->tensors, ((MLCtxTensor){ NULL, MLB_NAME_BLOCK_BEGIN }));
	log_debug2("ML block begin");
}

static inline
MLTensor* mlctx_tensor_add(MLCtx* C, const char* name, MLTensor* tensor)
{
	ggml_name_prefix(tensor, name);
	bool param = (tensor->op == GGML_OP_NONE);
	int id = strsto_add(C->ss, strsl_fromz(name));
	vec_push(C->tensors, ((MLCtxTensor){ tensor, id }));
	log_debug2("ML %s: %s " GGML_TYPESHAPE_FMT, param ? "param" : "op",
		name, GGML_TYPESHAPE_ARGS(tensor));
	return tensor;
}

static inline
MLTensor* mlctx_split_add(MLCtx* C, MLTensor* tensor)
{
	vec_push(C->tensors, ((MLCtxTensor){ tensor, MLB_NAME_SPLIT }));
	log_debug2("ML graph split");
	return tensor;
}

static inline
MLTensor* mlctx_input_new(MLCtx* C, const char* name, enum ggml_type dtype,
	int n0, int n1, int n2, int n3)
{
	MLTensor *T = ggml_new_tensor_4d(C->cp, dtype, n0,n1,n2,n3);
	ggml_set_name(T, name);
	ggml_set_input(T);
	vec_push(C->inputs, T);
	return T;
}

static inline
MLTensor* mlctx_param_new(MLCtx* C, const char* name, enum ggml_type dtype,
	int n0, int n1, int n2, int n3)
{
	MLTensor *T = ggml_new_tensor_4d(C->cp, dtype, n0,n1,n2,n3);
	ggml_set_input(T);
	return mlctx_tensor_add(C, name, T);
}
