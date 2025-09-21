
%: %.cc
	g++ -fopenmp -g -o $@ -std=c++2a -O3 -fassociative-math -ffast-math $< -lm
%-d: %.cc
	g++ -fsanitize=address -fopenmp -g -o $@ -std=c++2a $< -lm

