#!/bin/bash
#
# Run simulation, and compare result with the reference generated by create_reference.sh
#
# Usage
# ------
# run `./test.sh -h`
#

set -eu

# Where the FrontISTR project is checked out
FRONTISTR_HOME=$(cd $(dirname $BASH_SOURCE)/..;pwd)
CTEST_TEST_NAME=${CTEST_TEST_NAME:='.'}

echo_err () {
  ESC=$(printf '\033')
  echo "${ESC}[31m$1${ESC}[m" >&2
}

echo_success () {
  ESC=$(printf '\033')
  echo "${ESC}[32m$1${ESC}[m" >&2
}

check_executable () {
  if [[ ! -x $1 ]]; then
    echo_err "$1 is not executable"
    exit 1
  fi
}

usage () {
  cat <<EOM
Usage: $(basename "$0") [OPTION]...
  -h          Display this help
  -d VALUE    Target test case dir     (Default: .)
  -p VALUE    MPI processes            (Default: 1)
  -t VALUE    OpenMP threads           (Default: 1)
  -f VALUE    fistr1 binary path       (Default: fistr1)
  -e VALUE    hecmw_part1 binary path  (Default: hecmw_part1)
  -r VALUE    rmerge binary path       (Default: rmerge)
EOM
}

target=.
fistr1=fistr1
hecmw_part1=hecmw_part1
rmerge=rmerge
mpi_num_process=1
omp_num_threads=1
errors=0
while getopts ":d:p:t:f:e:r:h" optKey; do
  case "$optKey" in
    d)
      target=${OPTARG};;
    p)
      mpi_num_process=${OPTARG};;
    t)
      omp_num_threads=${OPTARG};;
    f)
      fistr1=${OPTARG};;
    e)
      hecmw_part1=${OPTARG};;
    r)
      rmerge=${OPTARG};;
    h)
      usage; exit 0;;
    *)
      usage; exit 1;;
  esac
done

if [ "$(shell mpicc --showme:version 2> /dev/null | grep 'open-mpi')" != "" ]; then
  mpi_options="--oversubscribe --allow-run-as-root"
else
  mpi_options=""
fi

compare_res=$FRONTISTR_HOME/tests/compare_res.pl

check_executable $fistr1
check_executable $hecmw_part1
check_executable $rmerge
check_executable $compare_res

test_dir=$FRONTISTR_HOME/run_test/$CTEST_TEST_NAME/$(date +"%Y%m%d%H%M")
mkdir -p $test_dir

for mesh_path in $(find $target -type f -name "*.msh"); do

  SECONDS=0
  ref_dir=$(cd $(dirname $mesh_path);pwd)
  mesh=$(basename $mesh_path)
  cnt=${mesh%.msh}.cnt
  res=${mesh%.msh}.res

  if [ ! -e $ref_dir/$cnt ]; then
    echo_err "*.cnt file for $mesh_path is not found. Skip this mesh file."
    continue
  fi

  pushd $test_dir > /dev/null
  echo "$mesh_path:"
  echo "  workdir: $test_dir"

  #
  # Ready files used in tests
  #
  cp -r $ref_dir/$mesh $test_dir
  cp -r $ref_dir/$cnt $test_dir
  if [ -e $ref_dir/istrain.dat ]; then
    cp -r $ref_dir/istrain.dat $test_dir
  fi
  [ $mpi_num_process -gt 1 ] && MESHTYPE=HECMW-DIST || MESHTYPE=HECMW-ENTIRE
cat <<EOL > hecmw_ctrl.dat
!MESH, NAME=fstrMSH,TYPE=$MESHTYPE
${mesh}
!CONTROL,NAME=fstrCNT
${cnt}
!RESULT,NAME=fstrRES,IO=OUT
${res}
!RESULT,NAME=vis_out,IO=OUT
${mesh}
!MESH, NAME=part_in,TYPE=HECMW-ENTIRE
${mesh}
!MESH, NAME=part_out,TYPE=HECMW-DIST
${mesh}
EOL
  if [ -e $ref_dir/is_tet_tet2.txt ]; then
    echo "!TET_TET2, ON" >> hecmw_ctrl.dat
  fi
  echo "!PARTITION,TYPE=NODE-BASED,METHOD=KMETIS,DOMAIN=$mpi_num_process" > hecmw_part_ctrl.dat

  #
  # Execute Tests
  #
  if [ $mpi_num_process -gt 1 ]; then
    $hecmw_part1 2>&1 \
      | tee hecmw_part1.log \
      | sed -e "s/^/    hecmw_part1 > /"
    mpirun $mpi_options -n $mpi_num_process \
      $fistr1 -t $omp_num_threads 2>&1 \
      | tee fistr1.log \
      | sed -e "s/^/    fistr1 > /"
  else
    $fistr1 -t $omp_num_threads 2>&1 \
      | tee fistr1.log \
      | sed -e "s/^/    fistr1 > /"
  fi

  #
  # Merge results generated by MPI to compare single-process result
  #
  find . -name "*.res.*" \
    | awk -F. '{print $NF}' \
    | sort \
    | uniq \
    | xargs -I{} $rmerge -n $mpi_num_process -s {} -e {} ${res} >/dev/null 2>&1

  #
  # Compare result
  #
  for t in $(find $ref_dir -name "${res}.*" | awk -F. '{print $NF}' | sort | uniq); do
    echo "  $res.$t:"
    TAGT=$PWD/$res.$t
    REF=$ref_dir/$res.0.$t
    if $compare_res $REF $TAGT; then
      echo_success "    result: success"
    else
      echo_err     "    result: failure"
      : $((errors++))
    fi
  done

  #
  # Clenaup
  #
  echo "  time: ${SECONDS}s"
  popd > /dev/null
done

[ $errors -ge 1 ] && exit $errors
exit 0
