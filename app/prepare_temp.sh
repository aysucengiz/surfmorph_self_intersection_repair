echo -e "\033[1;33m[1/7]\033[0m \033[1;96mDeleting old tmp files\033[0m"
BASE=~/Desktop # insert your dir that you want to work in
REPAIR=~/instant-mesh-intersection-repair # dir that you cloned the intersection repo to
VENV=$BASE/imrepair_cuda11
rm -rf $VENV
cd $BASE
rm -rf imrepair_cuda11/
echo -e "\033[1;33m[2/7]\033[0m \033[1;96mChecking Capacity\033[0m"
df -h /tmp
quota -s
mkdir -p $BASE

echo -e "\033[1;33m[3/7]\033[0m \033[1;96mInitializing venv\033[0m"
python3 -m venv $VENV
source $VENV/bin/activate

echo -e "\033[1;33m[4/7]\033[0m \033[1;96mPip Installs\033[0m"
pip install --no-cache-dir --upgrade pip setuptools wheel
pip install --no-cache-dir torch==1.13.1+cu116 --extra-index-url https://download.pytorch.org/whl/cu116
python -c "import torch; print(torch.__version__); print(torch.version.cuda); print(torch.cuda.is_available())"
nvcc --version


echo -e "\033[1;33m[5/7]\033[0m \033[1;96mDownload instant-mesh-intersection-repair requirements\033[0m"
export CUDA_SAMPLES_INC=$REPAIR/externals/cuda-samples/Common
cd $REPAIR
pip install --no-cache-dir numpy scipy PyYAML potpourri3d cholespy
pip install --no-cache-dir --force-reinstall "setuptools==69.5.1"

echo -e "\033[1;33m[6/7]\033[0m \033[1;96mDownload torch-mesh-isect requirements\033[0m"
cd $REPAIR/externals/torch-mesh-isect
rm -rf build mesh_intersection.egg-info
export CUDA_SAMPLES_INC=$REPAIR/externals/cuda-samples/Common
export CC=/usr/bin/gcc-10
export CXX=/usr/bin/g++-10
export CUDAHOSTCXX=/usr/bin/g++-10
pip install -r requirements.txt
python setup.py install

echo -e "\033[1;33m[7/7]\033[0m \033[1;96mFinal touches\033[0m"
python -c "import torch; import mesh_intersection; import bvh_cuda; print(torch.__version__, torch.version.cuda, torch.cuda.is_available()); print('mesh_intersection ok')"
pip install --no-cache-dir --force-reinstall "numpy<2"



