// Program 1: Vypocet faktorialu (iterativne, bez overeni celociselnosti)
import "ifj25" for Ifj
class Program {
    {
        {
            
        }
    }
    {
        static main() {
            var a 
            var b 
            b = ((3 - 2))
            a = 2 * (5 + 2 * 3) / 2
            {
                main()
                //return
            }
        }
    }
    
    static funkce(a, b) {
        if(a == 5) {
            a = 6
        } else {
            funkce(a, b)
            Ifj.write(a, b)
            a = 3
        }

        while(b < 2) {
            if(b == 0) {
                break
            } else {

            }
            
            b = b + 1
        }

        funkce(a, b)

        return a
    }
    
    static value {
        return __val
    }
    
    static value=(v) {
        __val = v
    }
    
    static f(a, b) {
        var a
        a = 5
        if(a == "a") {

        } else {
            Ifj.write("It woooooooorks")
            //a = Ifj.read() + b
        }
    }
}

class TvojeMama {
    
}


/*
static main() {
    Ifj.write("Zadejte cislo pro vypocet faktorialu\n")
    var a 
    a = Ifj.read_num()
    if (a != null) {
        if (a < 0) {
            Ifj.write("Faktorial ")
            Ifj.write(a)
            Ifj.write(" nelze spocitat\n")
        } else {
            var vysl 
            vysl = 1
            while (a > 0) {
                vysl = vysl * a
                a = a - 1
            }
            vysl = Ifj.floor(vysl)
            vysl = Ifj.str(vysl)
            vysl = "Vysledek: " + vysl + "\n"
            Ifj.write(vysl)
        }
    } else { // a == null
        Ifj.write("Faktorial pro null nelze spocitat\n")
    }
}
    */