// collatz.cpp — data-dependent trip counts: the static estimator cannot know
// them, the dynamic profiler measures them. Tests static-vs-dynamic ranking.
#include <iostream>
using namespace std;

int steps(int n) {
    int s = 0;
    while (n != 1) {
        if (n % 2 == 0) n = n / 2;
        else n = 3 * n + 1;
        s++;
    }
    return s;
}

int main() {
    int longest = 0;
    int arg = 0;
    for (int i = 1; i < 3000; i++) {
        int s = steps(i);
        if (s > longest) { longest = s; arg = i; }
    }
    cout << "longest chain below 3000 starts at " << arg << " (" << longest << " steps)" << endl;
    return 0;
}
