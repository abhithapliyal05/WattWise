// binsearch.cpp — many binary searches over a sorted table; short-circuit &&.
#include <iostream>
using namespace std;

int table[2048];

int find(int key, int n) {
    int lo = 0;
    int hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (table[mid] == key) return mid;
        if (table[mid] < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int main() {
    int n = 2048;
    for (int i = 0; i < n; i++) table[i] = i * 3;
    int hits = 0;
    for (int q = 0; q < 5000; q++) {
        int key = (q * 7) % 6200;
        int pos = find(key, n);
        if (pos >= 0 && table[pos] == key) hits++;
    }
    cout << "hits " << hits << endl;
    return 0;
}
