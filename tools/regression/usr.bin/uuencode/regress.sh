# $FreeBSD$

# Go into the regression test directory, handed to us by make(1)
TESTDIR=$1
if [ -z "$TESTDIR" ]; then
  TESTDIR=.
fi
cd $TESTDIR

STATUS=0

# To make sure we end up with matching headers.
umask 022

for test in traditional base64; do
  echo "Running test $test"
  case "$test" in
  traditional)
    uuencode regress.in < regress.in | diff -u regress.$test.out -
    ;;
  base64)
    uuencode -m regress.in < regress.in | diff -u regress.$test.out -
    ;;
  esac
  if [ $? -eq 0 ]; then
    echo "PASS: Test $test detected no regression, output matches."
  else
    STATUS=$?
    echo "FAIL: Test $test failed: regression detected.  See above."
  fi
done

exit $STATUS
