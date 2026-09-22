#include <iostream>
using namespace std;
int power(int b, int e) { if (e == 0) return 1; int h = power(b, e / 2); if (e % 2 == 0) return h * h; return h * h * b; }
int main() { cout << power(3, 10) << " " << power(2, 20) << endl; return 0; }
