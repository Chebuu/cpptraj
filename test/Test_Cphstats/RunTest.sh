#!/bin/bash

. ../MasterTest.sh

CleanFiles cphstats.in sorted.pH_*.00 stats.dat frac.agr implicit.sorted.dat

INPUT='-i cphstats.in'
TESTNAME='Constant pH stats / data set sort test'

UNITNAME='Ensemble data read / sort'
SKIP='no'
if [ ! -z $N_THREADS ] ; then
  if [ $N_THREADS -lt 7 ] ; then
    if [ $N_THREADS -eq 4 -o $N_THREADS -eq 5 ] ; then
      echo "  $UNITNAME cannot be run with 4 or 5 threads."
      ((CHECKERR++))
      SkipCheck "$UNITNAME"
      SKIP='yes'
    fi
  else
    CheckFor nthreads 6
    if [ $? -eq 1 ] ; then
      SKIP='yes'
    fi
  fi
fi
if [ "$SKIP" = 'no' ] ; then
  cat > cphstats.in <<EOF
readensembledata cpout.001 cpin cpin name PH
#readensembledata cpout.001 filenames cpout.002,cpout.003,cpout.004,cpout.005,cpout.006 name PH
list dataset
sortensembledata PH
list dataset
for i=0;i<6;i++ j=1;j++
  writedata sorted.pH_\$j.00 as cpout PH[*]%\$i nwriteheader 50 noensextension
done
EOF
  RunCpptraj "$UNITNAME"
  DoTest sorted.pH_1.00.save sorted.pH_1.00
  DoTest sorted.pH_2.00.save sorted.pH_2.00
  DoTest sorted.pH_3.00.save sorted.pH_3.00
  DoTest sorted.pH_4.00.save sorted.pH_4.00
  DoTest sorted.pH_5.00.save sorted.pH_5.00
  DoTest sorted.pH_6.00.save sorted.pH_6.00
fi

UNITNAME='Constant pH stats test'
cat > cphstats.in <<EOF
#readensembledata sorted.pH_1.00.save filenames sorted.pH_2.00.save,sorted.pH_3.00.save,sorted.pH_4.00.save,sorted.pH_5.00.save,sorted.pH_6.00.save cpin cpin name PH
readdata sorted.pH_1.00.save separate cpin cpin name PH1
readdata sorted.pH_2.00.save separate cpin cpin name PH2
readdata sorted.pH_3.00.save separate cpin cpin name PH3
readdata sorted.pH_4.00.save separate cpin cpin name PH4
readdata sorted.pH_5.00.save separate cpin cpin name PH5
readdata sorted.pH_6.00.save separate cpin cpin name PH6
list datasets
#runanalysis cphstats PH[*] statsout stats.dat fracplot fracplotout frac.agr deprot
runanalysis cphstats PH*[*] statsout stats.dat fracplot fracplotout frac.agr deprot
list dataset
EOF
RunCpptraj "$UNITNAME"
DoTest stats.dat.save stats.dat
DoTest frac.agr.save frac.agr

UNITNAME='Sorted implicit constant pH stats test'
cat > cphstats.in <<EOF
readdata md2_cpout.pH_2.00.save cpin 1AKI.dry.equil.cpin name PH
runanalysis cphstats PH[*] statsout implicit.sorted.dat
EOF
RunCpptraj "$UNITNAME"
DoTest implicit.sorted.dat.save implicit.sorted.dat

EndTest
exit 0
