#include <iostream>
using namespace std;
int counter = 0;
void bump(int k) { counter += k; }
int main() { for (int i = 1; i <= 4; i++) bump(i); cout << counter << endl; return 0; }
