main: http_server.cpp
	clang++ http_server.cpp -o http_server -I /usr/local/include -L /usr/local/lib -std=c++11 -Wall -pedantic -pthread -lboost_system
	clang++ console.cpp -o console.cgi -I /usr/local/include -L /usr/local/lib -std=c++11 -Wall -pedantic -pthread -lboost_system