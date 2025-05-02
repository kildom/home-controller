# home-controller


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
