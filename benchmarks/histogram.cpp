// histogram.cpp — bucket counting with nested conditionals and bools.
#include <iostream>
using namespace std;

int bucket[10];

int main() {
    int seed = 7;
    int evens = 0;
    for (int i = 0; i < 20000; i++) {
        seed = (seed * 75 + 74) % 65537;
        int x = seed % 100;
        bool even = x % 2 == 0;
        if (even) evens++;
        if (x < 50) {
            if (x < 25) bucket[x / 10] += 1;
            else bucket[x / 10] += 1;
        } else {
            bucket[x / 10] = bucket[x / 10] + 1;
        }
    }
    for (int b = 0; b < 10; b++) {
        cout << bucket[b];
        if (b < 9) cout << " ";
    }
    cout << endl;
    cout << "evens " << evens << endl;
    return 0;
}
