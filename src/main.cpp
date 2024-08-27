#include "editor.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
	std::string filepath;
	if (argc == 1) {
		std::cout << "Enter filename: ";
		getline(std::cin, filepath);
	}
	filepath = RESOURCES_PATH + filepath;
	editorStart(filepath.c_str());

	return 0;
}