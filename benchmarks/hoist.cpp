// hoist.cpp — the Review 1 example, now in plain C++: a loop-invariant
// expression recomputed on every iteration.
#include <iostream>
using namespace std;

int a[1000];

int main() {
    int n = 1000;
    int i = 0;
    while (i < n) {
        a[i] = (n * 2) + (n / 2);   // loop-invariant!
        i = i + 1;
    }
    cout << a[0] << " " << a[n - 1] << endl;
    return 0;
}
