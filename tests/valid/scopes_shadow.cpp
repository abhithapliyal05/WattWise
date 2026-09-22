#include <iostream>
using namespace std;
int x = 100;
int main() { cout << x << " "; int x = 1; { int x = 2; { int x = 3; cout << x; } cout << x; } cout << x << endl; return 0; }
