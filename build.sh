bazel build -c opt --copt=-O2 --copt=-mavx --copt=-mavx2 --copt=-mfma --copt=-msse4.2 --copt=-mf16c --copt=-Wno-sign-compare //tensorflow/tools/pip_package:build_pip_package --repo_env=WHEEL_NAME=tensorflow_cpu
ls -l bazel-bin/tensorflow/tools/pip_package/
