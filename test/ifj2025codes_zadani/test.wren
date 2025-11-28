import "ifj25" for Ifj
class Program {
    static main() {
        var result
        result = factorial(5)
        __d = Ifj.write(result)
    }

    static factorial(n) {
        if (n < 2) {
            return 1
        } else {
            var tmp
            tmp = factorial(n - 1)
            return n * tmp
        }
    }
}
