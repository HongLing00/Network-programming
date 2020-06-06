#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <map>
#include <vector>
#include <fstream>

using namespace std;
using namespace boost::asio;

map<int,map <string,string> > info;
io_context global_io_context;

string escape(string data){
    string session = "" ;
    for ( auto&& ch : data) session += ( "&#" + to_string(int(ch)) + ";" );
    return session;
}

void OutputShell(string session , string c){
    string content=escape(c);
    cout<<"<script>document.getElementById('"<<session<<"').innerHTML += '"<<content<<"';</script>"<<endl;
}

void OutputCommand(string session , string c){
    string content=escape(c);
    cout<<"<script>document.getElementById('"<<session<<"').innerHTML += '<b>"<<content<<"</b>';</script>"<<endl;    
}

void PrintConsoleTable(){
    cout<<"Content-type: text/html"<<"\r\n\r\n";
    cout<<"<!DOCTYPE html>" <<endl;
    cout<<"<html lang=\"en\">" <<endl;
    cout<<  "<head>" <<endl;
    cout<<      "<meta charset=\"UTF-8\" />" <<endl;
    cout<<      "<title>NP Project 3 Console</title>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "rel=\"stylesheet\"" <<endl;
    cout<<          "href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\"" <<endl;
    cout<<          "integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\"" <<endl;
    cout<<          "crossorigin=\"anonymous\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"" <<endl;
    cout<<          "rel=\"stylesheet\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "rel=\"icon\"" <<endl;
    cout<<          "type=\"image/png\"" <<endl;
    cout<<          "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<style>" <<endl;
    cout<<          "* {" <<endl;
    cout<<              "font-family: 'Source Code Pro', monospace;" <<endl;
    cout<<              "font-size: 1rem !important;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "body {" <<endl;
    cout<<              "background-color: #212529;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "pre {" <<endl;
    cout<<              "color: #87CEFA;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "b {" <<endl;
    cout<<              "color: #FFB7DD;" <<endl;
    cout<<          "}" <<endl;
    cout<<      "</style>" <<endl;
    cout<<  "</head>" <<endl;
    cout<<  "<body>" <<endl;
    cout<<      "<table class=\"table table-dark table-bordered\">" <<endl;
    cout<<          "<thead>" <<endl;
    cout<<              "<tr>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[0]["HOST"]<<":"<<info[0]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[1]["HOST"]<<":"<<info[1]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[2]["HOST"]<<":"<<info[2]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[3]["HOST"]<<":"<<info[3]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[4]["HOST"]<<":"<<info[4]["PORT"]<<"</th>" <<endl;
    cout<<              "</tr>" <<endl;
    cout<<          "</thead>" <<endl;
    cout<<          "<tbody>" <<endl;
    cout<<              "<tr>" <<endl;
    cout<<                  "<td><pre id=\"s0\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s1\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s2\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s3\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s4\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<              "</tr>" <<endl;
    cout<<          "</tbody>" <<endl;
    cout<<      "</table>" <<endl;
    cout<<  "</body>" <<endl;
    cout<<"</html>" <<endl;
}

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

void SetConnectionInfo(){
    string qstr=GEnv("QUERY_STRING");
    vector<string> env = SplitString(qstr,"&");
    int pos=0;
    for(int i=0;i<15;i++){
        pos=env[i].find("=");
        if((i%3)==0){
            info[(i/3)]["HOST"]= pos<env[i].length()-1 ? env[i].substr(pos+1) : "" ;
        }else if((i%3)==1){
            info[(i/3)]["PORT"]= pos<env[i].length()-1 ? env[i].substr(pos+1) : "" ;
        }else{
            info[(i/3)]["FILE"]= pos<env[i].length()-1 ? "test_case/"+env[i].substr(pos+1) : "" ;
        }
    }
}

class Session : public enable_shared_from_this<Session> {
    private:
        enum { max_length = 1024 };
        ip::tcp::socket _socket;
        array<char, max_length> _data;
        string s;
        ip::tcp::resolver _resolver;
        int ID;
        string session;
        ifstream testfile;
        deadline_timer timer;
    public:
        Session(int id) : 
            _socket(global_io_context) ,_resolver(global_io_context),
            ID(id),session("s"+to_string(id)),testfile(info[id]["FILE"])
            ,timer(global_io_context) {}

        void start() {
            do_resolve();
        }

    private:
        void do_resolve(){
            auto self(shared_from_this());
            _resolver.async_resolve( ip::tcp::resolver::query(info[ID]["HOST"],info[ID]["PORT"]),
                [this,self](boost::system::error_code ec,ip::tcp::resolver::iterator iterator) {
                    if (!ec) {
                        do_connect(iterator);
                    }
                }
            );
        }

        void do_connect(ip::tcp::resolver::iterator iterator) {
            auto self(shared_from_this());
            _socket.async_connect( *iterator,[this,self](boost::system::error_code ec) {
                if (!ec) {
                    do_read();
                }
            });
        }

        void do_read() {
            auto self(shared_from_this());
            _socket.async_read_some(buffer(_data, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if(!ec){
                        s="";
                        for(int i=0;i<length;i++){
                            s+=_data[i];
                        }
                        OutputShell(session,s);
                        if(s.find("%")!=string::npos){
                            timer.expires_from_now(boost::posix_time::seconds(1));
                            timer.async_wait(
                                [this, self](boost::system::error_code ec) {
                                    do_write();
                                }
                            );
                        }
                        do_read();
                    }
                }
            );
        }

        void do_write() {
            auto self(shared_from_this());
            string line="";
            getline(testfile,line);
            if(line=="exit"){
                testfile.close();
            }
            line += '\n';
            OutputCommand(session,line);
            async_write(_socket,buffer(line.c_str(), line.length()),
                [this, self](boost::system::error_code ec, std::size_t length ) {
                    if(!ec) {
                        do_read();
                    }
                }
            );
        }
};

int main(){
    SetConnectionInfo();
    PrintConsoleTable();
    try {
        for(int i=0;i<5;i++){
            make_shared<Session>(i)->start();
        }
        global_io_context.run();
    }catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
}