echo "1. Loading dependencies (on BEAST)..."
module load intel/19.1.1 # just to use mpirun

module use ~/.module
module use ~/loc-libs/spack/share/spack/modules/linux-sles15-zen2

module load cmake-3.20.1-gcc-10.2.1-7cjd5mz
module load argobots-1.1-gcc-10.2.1-s3e2vao
module load boost-1.76.0-gcc-10.2.1-h37ct6b
module load cereal-1.3.0-gcc-10.2.1-vd6dtp3
module load libfabric-1.11.1-gcc-10.2.1-7rkzvhv # this one is built with an updated name of rdma-core on beast
module load mercury-2.0.1rc3-gcc-10.2.1-565ptkn # this one is built with an updated name of rdma-core on beast
module load mochi-abt-io-0.5.1-gcc-10.2.1-rghdmos
module load mochi-margo-0.9.4-gcc-10.2.1-7sqzydv
module load mochi-thallium-0.7-gcc-10.2.1-hhkhxqk

## -----------------------------------------
## -------- Running server -----------------
echo "Run the server on ROME1..."
echo "   mpirun -n 2 --host rome1,rome2 ./tl_mxm_rdma_transf"
mpirun -n 2 --host rome1,rome2 ./tl_mxm_rdma_transf