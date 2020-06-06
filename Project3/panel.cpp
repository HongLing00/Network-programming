#include <string>
#include <iostream>

using namespace std;

const int N_SERVERS = 5;

const string FormMethod = "GET";
const string FormAction = "console.cgi";
const string TestCaseDirectory = "test_case";
const string Domain = "cs.nctu.edu.tw";

int main() {

	string test_case_menu = "";
	string host_menu = "";

	for (int i = 1; i < 11; i++) {
		test_case_menu += ("<option value=t" + to_string(i) + ".txt>t" + to_string(i) + ".txt</option>");
	}

	for (int i = 1; i < 11; i++) {
		if (i < 6) {
			host_menu += ("<option value=nplinux" + to_string(i) + "." + Domain + ">nplinux" + to_string(i) + "</option>");
		}
		else {
			host_menu += ("<option value=npbsd" + to_string(i - 5) + "." + Domain + ">npbsd" + to_string(i - 5) + "</option>");
		}
	}
	cout << "HTTP / 1.1 200 OK\n";

	cout << "Content-type: text/html\r\n\r\n";

	cout << "<!DOCTYPE html>\n";
	cout << "<html lang=\"en\">\n";
	cout << "	<head>\n";
	cout << "		<title>NP Project 3 Panel</title>\n";
	cout << "		<link\n";
	cout << "			rel=\"stylesheet\"\n";
	cout << "			href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\"\n";
	cout << "			integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\"\n";
	cout << "			crossorigin=\"anonymous\"\n";
	cout << "		/>\n";
	cout << "		<link\n";
	cout << "			href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n";
	cout << "			rel=\"stylesheet\"\n";
	cout << "		/>\n";
	cout << "		<link\n";
	cout << "			rel=\"icon\"\n";
	cout << "			type=\"image/png\"\n";
	cout << "			href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\"\n";
	cout << "		/>\n";
	cout << "		<style>\n";
	cout << "			* {\n";
	cout << "				font-family: 'Source Code Pro', monospace;\n";
	cout << "			}\n";
	cout << "		</style>\n";
	cout << "	</head>\n";
	cout << "	<body class = \"bg-secondary pt-5\">";

	cout << "		<form action = \"" + FormAction + "\" method = \"" + FormMethod + "\">\n";
	cout << "			<table class = \"table mx-auto bg-light\" style = \"width: inherit\">\n";
	cout << "				<thead class = \"thead-dark\">\n";
	cout << "					<tr>\n";
	cout << "						<th scope = \"col\">#</th>\n";
	cout << "						<th scope = \"col\">Host</th>\n";
	cout << "						<th scope = \"col\">Port</th>\n";
	cout << "						<th scope = \"col\">Input File</th>\n";
	cout << "					</tr>\n";
	cout << "				</thead>\n";
	cout << "				<tbody>\n";

	for (int i = 0; i < N_SERVERS; i++) {
		cout << "				<tr>\n";
		cout << ("					<th scope = \"row\" class = \"align-middle\">Session " + to_string(i + 1) + "</th>\n");
		cout << "					<td>\n";
		cout << "						<div class = \"input-group\">\n";
		cout << ("							<select name = \"h" + to_string(i) + "\" class = \"custom-select\">\n");
		cout << ("								<option></option>" + host_menu + "\n");
		cout << "							</select>\n";
		cout << "							<div class = \"input-group-append\">\n";
		cout << "								<span class = \"input-group-text\">.cs.nctu.edu.tw</span>\n";
		cout << "							</div>\n";
		cout << "						</div>\n";
		cout << "					</td>\n";
		cout << "					<td>\n";
		cout << ("						<input name = \"p" + to_string(i) + "\" type = \"text\" class = \"form-control\" size = \"5\" />\n");
		cout << "					</td>\n";
		cout << "					<td>\n";
		cout << ("						<select name = \"f" + to_string(i) + "\" class = \"custom-select\">\n");
		cout << "							<option></option>\n";
		cout << ("							" + test_case_menu + "\n");
		cout << "						</select>\n";
		cout << "					</td>\n";
		cout << "				</tr>";
	}
	cout << "					<tr>\n";
	cout << "						<td colspan=\"3\"></td>\n";
	cout << "						<td>\n";
	cout << "							<button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>\n";
	cout << "						</td>\n";
	cout << "					</tr>\n";
	cout << "				</tbody>\n";
	cout << "			</table>\n";
	cout << "		</form>\n";
	cout << "	</body>\n";
	cout << "</html>";
	return 0;
}