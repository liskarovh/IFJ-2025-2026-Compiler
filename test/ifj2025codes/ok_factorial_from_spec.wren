import "ifj25" for Ifj
class Program {
  static main() {
    Ifj.write("Zadejte cislo pro vypocet faktorialu: ")
    var inp = Ifj.read_num()
    if (inp != null) {
      if (inp < 0) {
        Ifj.write("Faktorial nelze spocitat!\n")
      } else {
        var flo = Ifj.floor(inp)
        if (inp == flo) {
          var vysl = factorial(inp)
          vysl = Ifj.str(vysl)
          Ifj.write("Vysledek: "); Ifj.write(vysl)
        } else {
          Ifj.write("Cislo neni cele!\n")
        }
      }
    } else {
      Ifj.write("Chyba pri nacitani celeho cisla!\n")
    }
  }
  static factorial(n) {
    var result
    if (n < 2) { result = 1 } else {
      var d = n - 1
      result = factorial(d)
      result = n * result
    }
    return result
  }
}
