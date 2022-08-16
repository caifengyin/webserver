CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2
endif


# $(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient   # Ubuntu 

server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) $$(mysql_config --cflags --libs)   -lpthread -g
clean:
	rm  -r server
