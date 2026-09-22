#include <iostream>
using namespace std;
const int A = 6;
const int B = A * 7;
int main() { int v[B / 6]; v[0] = B; cout << v[0] << " " << (A + B) * 2 << endl; return 0; }
