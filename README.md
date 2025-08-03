# home-controller

* Obudowa: [Kradex ZD1004J](https://botland.com.pl/obudowy/24954-obudowa-modulowa-kradex-zd1004j-abs-v0-na-szyne-din-652x898x716mm-jasnoszara-5905275033614.html)
* Przekaźniki: FORWARD INDUSTRIAL FRM18A-5
* Zaciski 230V: AK3001/6-KD-5.0/KIES
* Zaciski IO/12V: Phoenix Contact PTSA 0,5/ 8-2,5-Z 1990067
* Zabezpieczenie ESD: Littelfuse SP720
* Mikrokontroller: 2x AVR64DD28-I/SO
* Pojedynczy moduł:
  * 11 przekaźników:
    * 9x wspólny zmostkowany styk
    * 1x niezależny z dwoma stykami
    * 1x podłączony do strony styków IO/12V
  * 12 styków 230V
  * 22 styki IO/12V: 8 + 8 + 6
    * 2x2x LED driver
    * 2x Open-Collector
    * 14x IO
    * 1x2x Relay
  * 2x Złącze między modułami (żeby można było łączyć szeregowo):
    * Łączone przez złącze żeńskie kątowe na płytkach po bokach i kołki między modułami (bez kabla), np. [SL 22 164 10 G](https://www.tme.eu/pl/details/sl22.164.10.g)
      * Moduł wykonawczy i moduł zasilający mogą mieć płytki na różnych wysokościach (ok 1.4mm mod. zasilający jest wyżej), więc może trzeba obniżyć jedną, podnieś drugą, lutować złącza inaczej, albo wszystkiego po trochu.
    * 3x Masa
    * 5V
    * 2x 12V
    * UART
    * RS-485 rxd
    * RS-485 txd
    * RS-485 DE
      * W module zasilającym jest pull-down
      * W drugim zączu jest pull-up
      * W ten sposób kontroller będzie rozróżniał, czy został podłączony do moduły zasilającego i jest przekaźnikiem, czy też został podłączony do innego modułu i jest końcowym urządzeniem.
* Moduł zasilający
  * Stabilizator 12V->5V
  * Driver RS-485
  * Wiele wejść 12V: 1 podstawowe, 1 awaryjne
  * 1x Złącze między modułami
  * Zaciski:
    * 2x Masa
    * 2x 12V
    * 2x RS-485
  * Może zmieści się do:
    * Obudowa: [Kradex Z103J 88x34x62mm](https://botland.com.pl/obudowy/10131-obudowa-modulowa-kradex-z103j-88x34x62mm-na-szyne-din-jasna-5905275012343.html),
    * Zaciski: [TBJ05-03-1-G-G](https://www.tme.eu/pl/details/tbj05-03-1-g-g) (albo podobne na 6 torów)
    * Wszystkie zaciski powinny być z jednej strony (od strony niskiego napięcia)

# Stare informacje

Sprzęt:
* Do skrzynek: NUCLEO-F446ZE https://kamami.pl/stm-nucleo-144/560902-nucleo-f446ze-plytka-rozwojowa-z-mikrokontrolerem-stm32f446zet6-5906623410026.html
* Do skrzynki komunikacyjnej: NUCLEO-F439ZI https://kamami.pl/stm-nucleo-144/572182-nucleo-f439zi-plytka-rozwojowa-z-mikrokontrolerem-stm32f439zit6-5906623410378.html
* Ochrona IO: SP720

Funkcjonalność:
* Napięcie zasilania: 12V, przekażniki, sterowanie diodami
* Napięcie mikrokontrolera: 3.3V
* Kilka wejść zasilania: jedno głowne (z dwiema diodami równolegle z niskim spadkiem), 3-4 dodatkowe z dwiema diodami szeregowo
* Zasilanie 230V jedno przy UPSie z przełączaniem na zasilanie awaryjne
* Wykrywanie jaki tryb zasilania jest aktualnie: normalny czy z UPSa
* Bezpiecznik w kaźdej skrzynce i jedna róźnicówka przy UPSie
* Komunikacja między kontrolerami przez powolny UART na RS-485
* Komunikacja ze światem zewnętrznym przez Ethernet i BLE
* Wyjścia przekażnikowe 230V (tylko NO wystarczą), dużo: lampy (też awaryjne), i rolety (2x na jedną roletę)
* Wyjścia LED, sterowanie prądem - wyjście PWM ze sprzęźeniem zwrotnym na ADC, wyrównanie napięcia przez R i C (PNP, NPN, 4 rezystory i kondensator)
* Wejścia-wyjścia niskonapięciowe 3.3V z SP720, kondensatorem i rezystorem
* Wejścia-wyjścia niskonapięciowe są do: przełączniki, komunikacja, i.t.p.
* Flashowanie za pośrednistwem Ethernet/BLE i rozsyłane dalej do kolenych płytek, jeżeli potrzeba
