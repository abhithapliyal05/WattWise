#include <iostream>
using namespace std;
int classify(int n) { if (n < 0) return -1; else if (n == 0) return 0; else { if (n > 100) return 2; return 1; } }
int main() { cout << classify(-5) << classify(0) << classify(7) << classify(500) << endl; return 0; }
