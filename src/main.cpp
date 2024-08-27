#include "editor.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
	std::string filepath;
	if (argc == 1) {
		std::cout << "Enter filename (or just press enter): ";
		getline(std::cin, filepath);
	}
	filepath = filepath;
	if (filepath.empty()) {
		editorStart(nullptr);
	} else {
		editorStart(filepath.c_str());
	}
	return 0;
}