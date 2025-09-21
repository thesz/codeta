# Use of decision tables for text prediction

This repository contains source files of my experiments regarding use of decision tables in the data compression.

Makefile contains rules to build .cc files.

There is a single program right now, deta.cc, you can complie it with ``make deta``.

# ``deta`` program

``deta`` program reads enwik8 (can be downloaded here) into memory and builds decision trees for bits from highest one (7th) to the lowest one (0th). Bit 6 utilizes bit 7 as the tree's input variable, bit 5 utilizes bits 7 and 6 and so on.

As ``deta`` is quite chatty, you should write it's output into the log file.

After execution you can gather statistics, for example, grepping with ``egrep "bit [0-9]|adjusted" log-file`` will print bits indices and their trees' final bit lengths and accuracies. Currently, for trees of 65536 leafs at most, total bit length is 2.02 bits per byte.

Grepping with ``egrep "bit [0-9]|unsplit (1|3|7|15|31|63|127|255|511|1023|2047|4095|8191|16383|32767|65535)," log-file`` will tell you something about table size prediction scaling properties. For example, doubling tables' size can be expected to shave off about 0.18 bits from total bit lengths, thus it will be close to LSTM RNN prediction power.

# Why?

Decision tables are fast tp work with. 65536 leafs translates into about 16 (probably, less) decision operations per bit predicted, 128 decision operations per byte. They allow to predict bits from arbitrary location, if you provide context wihndow for that location, thus, they allow for (semi-) random access.
