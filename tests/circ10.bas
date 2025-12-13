10 PRINT "Starting test with";10;"nested GOSUBs"
20 GOSUB 1000
30 PRINT "Back to main - SUCCESS!"
40 END
1000 PRINT "Enter level 1"
1010 GOSUB 1100
1020 PRINT "Return from level 1"
1030 RETURN
1100 PRINT "Enter level 2"
1110 GOSUB 1200
1120 PRINT "Return from level 2"
1130 RETURN
1200 PRINT "Enter level 3"
1210 GOSUB 1300
1220 PRINT "Return from level 3"
1230 RETURN
1300 PRINT "Enter level 4"
1310 GOSUB 1400
1320 PRINT "Return from level 4"
1330 RETURN
1400 PRINT "Enter level 5"
1410 GOSUB 1500
1420 PRINT "Return from level 5"
1430 RETURN
1500 PRINT "Enter level 6"
1510 GOSUB 1600
1520 PRINT "Return from level 6"
1530 RETURN
1600 PRINT "Enter level 7"
1610 GOSUB 1700
1620 PRINT "Return from level 7"
1630 RETURN
1700 PRINT "Enter level 8"
1710 GOSUB 1800
1720 PRINT "Return from level 8"
1730 RETURN
1800 PRINT "Enter level 9"
1810 GOSUB 1900
1820 PRINT "Return from level 9"
1830 RETURN
1900 PRINT "Enter level 10"
1920 PRINT "Return from level 10"
1930 RETURN
