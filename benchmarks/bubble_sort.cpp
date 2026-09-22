// bubble_sort.cpp — O(n^2) sort of pseudo-random vals; branch-heavy inner loop.
#include <iostream>
using namespace std;

int vals[300];

int main() {
    int n = 300;
    int seed = 12345;
    for (int i = 0; i < n; i++) {
        seed = (seed * 75 + 74) % 65537;   // small LCG, no overflow
        vals[i] = seed % 1000;
    }
    for (int pass = 0; pass < n - 1; pass++) {
        for (int j = 0; j < n - 1 - pass; j++) {
            if (vals[j] > vals[j + 1]) {
                int tmp = vals[j];
                vals[j] = vals[j + 1];
                vals[j + 1] = tmp;
            }
        }
    }
    bool sorted = true;
    for (int i = 1; i < n; i++)
        if (vals[i - 1] > vals[i]) sorted = false;
    cout << "sorted=" << sorted << " min=" << vals[0] << " max=" << vals[n - 1] << endl;
    return 0;
}
