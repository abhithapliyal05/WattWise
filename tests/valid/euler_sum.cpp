#include <iostream>
using namespace std;
// Sum of multiples of 3 or 5 below a bound, plus a digit-sum helper.
int digitSum(int n) { int s = 0; while (n > 0) { s += n % 10; n /= 10; } return s; }
int main() {
    int total = 0;
    for (int i = 1; i < 1000; i++) {
        if (i % 3 == 0 || i % 5 == 0) total += i;
    }
    cout << total << " " << digitSum(total) << endl;
    return 0;
}
