#include <iostream>
using namespace std;
int main() { int c = 0; for (int i = 0; i < 6; i++) for (int j = i; j < 6; j++) c += i * j; cout << c << endl; return 0; }
