"""
Modal script to generate Accel-Sim SASS traces on H100 (SM90).

Usage:
    conda activate modal
    modal run trace_h100.py

Traces will be downloaded to ./traces_output/ locally.
"""

import modal
import os

# ---------- Modal image: CUDA 12.8 + build tools + tracer source ----------
tracer_nvbit_path = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

image = (
    modal.Image.from_registry(
        "nvidia/cuda:12.8.0-devel-ubuntu22.04",
        add_python="3.11",
    )
    .apt_install(
        "wget", "bzip2", "build-essential", "bc", "git",
    )
    .add_local_dir(
        tracer_nvbit_path,
        remote_path="/root/tracer_nvbit",
        ignore=lambda p: any(
            x in str(p) for x in ["nvbit_release", ".git", "__pycache__", "modal"]
        ),
    )
)

app = modal.App("accel-sim-h100-tracer", image=image)

# ---------- Vector Add CUDA source ----------
VECADD_CU = r"""
#include <stdio.h>
#include <cuda_runtime.h>

__global__ void vectorAdd(const float *A, const float *B, float *C, int N) {
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < N) {
        C[i] = A[i] + B[i];
    }
}

int main() {
    const int N = 1 << 20;  // 1M elements
    size_t size = N * sizeof(float);

    float *h_A = (float *)malloc(size);
    float *h_B = (float *)malloc(size);
    float *h_C = (float *)malloc(size);

    for (int i = 0; i < N; i++) {
        h_A[i] = 1.0f;
        h_B[i] = 2.0f;
    }

    float *d_A, *d_B, *d_C;
    cudaMalloc(&d_A, size);
    cudaMalloc(&d_B, size);
    cudaMalloc(&d_C, size);

    cudaMemcpy(d_A, h_A, size, cudaMemcpyHostToDevice);
    cudaMemcpy(d_B, h_B, size, cudaMemcpyHostToDevice);

    int threadsPerBlock = 256;
    int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;
    vectorAdd<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, N);

    cudaMemcpy(h_C, d_C, size, cudaMemcpyDeviceToHost);

    // Quick verification
    bool ok = true;
    for (int i = 0; i < N; i++) {
        if (h_C[i] != 3.0f) { ok = false; break; }
    }
    printf("VectorAdd: %s\\n", ok ? "PASSED" : "FAILED");

    cudaFree(d_A); cudaFree(d_B); cudaFree(d_C);
    free(h_A); free(h_B); free(h_C);
    return 0;
}
"""


@app.function(
    gpu="H100",
    timeout=600,
)
def generate_traces():
    import subprocess

    def run(cmd, cwd=None):
        print(f"\n>>> {cmd}")
        result = subprocess.run(
            cmd, shell=True, cwd=cwd,
            capture_output=True, text=True,
        )
        if result.stdout:
            print(result.stdout[-2000:])  # tail output
        if result.returncode != 0:
            print(f"STDERR:\n{result.stderr[-2000:]}")
            raise RuntimeError(f"Command failed (rc={result.returncode}): {cmd}")
        return result.stdout

    workdir = "/root/tracer_nvbit"

    # 0. Show GPU info
    run("nvidia-smi")

    # 1. Install NVBit
    run("bash install_nvbit.sh", cwd=workdir)

    # 2. Build tracer_tool
    run("make clean && make -j", cwd=f"{workdir}/tracer_tool")

    # 3. Build post-traces-processing
    run("make clean && make -j", cwd=f"{workdir}/tracer_tool/traces-processing")

    # 4. Write and compile vector_add
    app_dir = "/root/vector_add"
    os.makedirs(app_dir, exist_ok=True)
    with open(f"{app_dir}/vector_add.cu", "w") as f:
        f.write(VECADD_CU)
    run("nvcc -o vector_add vector_add.cu", cwd=app_dir)

    # 5. Run vector_add with tracer to generate traces
    trace_dir = f"{app_dir}/traces"
    os.makedirs(trace_dir, exist_ok=True)
    run(
        f"LD_PRELOAD={workdir}/tracer_tool/tracer_tool.so "
        f"./vector_add",
        cwd=app_dir,
    )

    # 6. Post-process traces
    #    The tracer generates kernelslist files with context suffixes,
    #    e.g. kernelslist_ctx_0x55f46a633cc0. Find and process all of them.
    import glob
    kernelslists = glob.glob(f"{app_dir}/traces/kernelslist*")
    if kernelslists:
        for kl in kernelslists:
            print(f"Post-processing: {kl}")
            run(
                f"{workdir}/tracer_tool/traces-processing/post-traces-processing "
                f"{kl}",
                cwd=app_dir,
            )
    else:
        print("WARNING: No kernelslist files found!")
        run(f"find {app_dir} -type f")

    # 7. Collect all output files
    result_files = {}
    trace_output = f"{app_dir}/traces"
    for root, dirs, files in os.walk(trace_output):
        for fname in files:
            full = os.path.join(root, fname)
            rel = os.path.relpath(full, trace_output)
            with open(full, "rb") as f:
                result_files[rel] = f.read()
            print(f"  Collected: {rel} ({len(result_files[rel])} bytes)")

    return result_files


@app.local_entrypoint()
def main():
    """Run trace generation on Modal H100, download results locally."""
    print("Launching H100 trace generation on Modal...")
    result_files = generate_traces.remote()

    # Save traces locally
    output_dir = os.path.join(os.path.dirname(__file__), "traces_output")
    os.makedirs(output_dir, exist_ok=True)

    for rel_path, data in result_files.items():
        dest = os.path.join(output_dir, rel_path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
        print(f"  Saved: {dest} ({len(data)} bytes)")

    print(f"\nDone! Traces saved to {output_dir}/")
    print("Use these traces with Accel-Sim:")
    print(f"  kernelslist.g -> {output_dir}/kernelslist.g")
