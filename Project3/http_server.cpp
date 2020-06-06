#include <iostream>
#include <boost/asio.hpp>
#include <signal.h>
#include <utility>
#include <set>
#include <sys/wait.h> //waitpid()
#include <sys/types.h> //pid_t
#include <vector>

using namespace std;
using namespace boost::asio;

const string httpok ="HTTP/1.1 200 OK";

io_service global_io_service;

string GEnv(string name){
    char* value=getenv(name.c_str());
    if(value!=NULL){
        return value;
    }
    return "";
}

void SEnv(string name ,string value){
    setenv(name.c_str(),value.c_str(),1);
}

vector<string> SplitString(const string line, const string delimeter){
    vector <string> v;
    size_t pos1, pos2;
    //從頭開始find
    pos1 = 0;
    //第一個空格的位址
    pos2 = line.find(delimeter);
    //find失敗會returnstring::npos
    while(string::npos != pos2){
        //將第一個substring push到vector
        v.push_back(line.substr(pos1, pos2-pos1));
        //從空格的下一個位址開始find
        pos1 = pos2 + delimeter.size();
        //下一個空格的位址
        pos2 = line.find(delimeter, pos1);
    }
  //將最後一個substring push到vector
    if(pos1 != line.length()){
        v.push_back(line.substr(pos1));
    }
    return v;
}

void ChildHandler(int signo){
    int status;
    while(waitpid(-1,&status,WNOHANG)>0){}
}

class Session : public enable_shared_from_this<Session> {
    private:
        enum { max_length = 1024 };
        ip::tcp::socket _socket;
        array<char, max_length> _data;
        string s;
        string cgi;
    public:
        Session(ip::tcp::socket socket) : _socket(move(socket)) {}

        void start() { do_read(); }

    private:
        void do_read() {
            auto self(shared_from_this());
            _socket.async_read_some(buffer(_data, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if(!ec) {
                        s="";
                        for(int i=0;i<length;i++){
                            s+=_data[i];
                        }
                        cout<<s<<endl;
                        do_write();
                    }
                }
            );
        }
        void do_write() {
            auto self(shared_from_this());
                boost::system::error_code ec;
                ip::tcp::endpoint lendpoint = _socket.local_endpoint(ec);
                if(!ec){
                    SEnv("SERVER_ADDR",lendpoint.address().to_string());
                    SEnv("SERVER_PORT",to_string(lendpoint.port()));
                }else{
                    cout<<"local "<<ec.message()<<endl;
                }
                ip::tcp::endpoint rendpoint = _socket.remote_endpoint(ec);
                if(!ec){
                    SEnv("REMOTE_ADDR",rendpoint.address().to_string());
                    SEnv("REMOTE_PORT",to_string(rendpoint.port()));
                }else{
                    cout<<"remote "<<ec.message()<<endl;
                }
                vector <string> env = SplitString(s,"\r\n");
                vector <string> v1 = SplitString(env[0]," ");//切出REQUEST_METHOD,REQUEST_URI,SERVER_PROTOCOL
                vector <string> v2 = SplitString(v1[1],"?"); //切出QUERY_STRING
                vector <string> v3 = SplitString(env[1],":");//切出HTTP_HOST
                SEnv("REQUEST_METHOD",v1[0]);
                SEnv("REQUEST_URI",v1[1]);
                SEnv("SERVER_PROTOCOL",v1[2]);
                cgi=v2[0].substr(1);
                SEnv("QUERY_STRING",v2[1]);
                SEnv("HTTP_HOST",v3[1].substr(1));//前面會有一個空白
                _socket.async_send(buffer(httpok.c_str(), httpok.length()),
                    [this, self](boost::system::error_code ec, std::size_t /* length */) {
                        if(!ec) {
                            if(fork()==0){
                                char** arg = new char* [2];
                                cout<<"=="<<cgi<<"=="<<endl;
                                arg[0]=strdup(cgi.c_str());
                                arg[1]=NULL;
                                dup2(_socket.native_handle(),0);
                                dup2(_socket.native_handle(),1);
                                dup2(_socket.native_handle(),2);
                                if(execv(arg[0],arg)<0){
                                    cerr<<strerror(errno)<<endl;
                                    exit(EXIT_FAILURE);
                                }
                            }
                            _socket.close();
                        }
                    }
                );
        }
};

class Server {
    private:
        ip::tcp::acceptor _acceptor;
        ip::tcp::socket _socket;

    public:
        Server(short port)
            : _acceptor(global_io_service, ip::tcp::endpoint(ip::tcp::v4(), port)),_socket(global_io_service) { do_accept(); }

    private:
        void do_accept() {
            _acceptor.async_accept(_socket, 
                [this](boost::system::error_code ec){
                    if (!ec) {
                        make_shared<Session>(move(_socket))->start();
                    do_accept();
                    }
                }
            );
        }
};

int main(int argc ,char* const argv[]){
    if (argc != 2) {
        std::cerr << "Usage:" << argv[0] << " [port]" << endl;
        return 1;
    }
    signal(SIGCHLD,ChildHandler);
    try {
        short port = atoi(argv[1]);
        Server server(port);
        global_io_service.run();
    }catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}