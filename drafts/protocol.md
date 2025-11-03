

# Network Layers

## 1. Physical Layer

* Medium:
  * Half duplex RS-485
  * Half duplex 1-wire UART
* Wszystkie urządzenia są równorzędne
* Urządzenie domyśnie jest w stanie nadsłuchiwania UART
* Łącze uznane jest za dostępne, jeżeli ostatni pakiet się zakończył lub nic nie jest transmitowane przed odpowiedni czas (w przypadku, gdy nie jest znany ostani pakiet)
* Jeżeli urządzenie chce nadawać, czeka losowy czas (rozkład zależny od długości kolejki, czasu oczekiwania pierwszego pakietu w kolejce i najwyższego priorytetu pakietów z kolejki)
  * Przechodzi to trybu GPIO
  * Ustawia DataOutput=0
  * Sprawdza ostani raz, czy nikt inny nie zaczął nadawania (stan niski)
  * Ustawia TxEnable=1
  * Czeka 1 Tbit
  * Ustawia DataOutput=1
  * Czeka ~0.25 Tbit
  * Ustawia TxEnable=0
  * Czeka 8.75 Tbit
  * Czeka losowy czas 0..10Tbit (włączając czasy ułamkowe)
  * Powtarza jeszcze 2 razy
  * Jeżeli w tym czasie wykryto, że ktoś inny chce nadawać zaprzestajemy nadawania, przechodzimy do trybu UART i czekamy określony czas zanim znowu uznamy łącze za dostępne.
  * Przechodzi do trybu UART, TxEnable=1
  * ```
      <-- 10 --><- 0..10 -><-- 10 --><- 0..10 -><-- 10 -->
    -- ---------........... ---------........... ---------DDDDDDDDDDD
      -                    -                    -         DDDDDDDDDDD
    ```
  * Przesyłamy pakiet
  * Przechodzi do trybu UART, TxEnable=0
  * Praktyczne podejście na STM32:
    * (RS-485)
    * Ustaw TIMER na one-pulse mode
    * DataOut = 0, wyłącz przerwania, sprawdza input, TxEn = 1, włącz TIMER, włącz przerwania
    * Timer po czasie 1 Tbit ustawia DataOut = 1, po 1.25 Tbit w przerwaniu ustawia TxEn = 0 i przełącza wejście na nadsłuchiwanie IRQ
    * Timer po czasie 10 + rand w przerwaniu ponawia próbę
    * LUB, jeżeli już nie ma więcej prób, timer po czasie 10 w przerwaniu rozpoczyna transmisję
    * (1-wire UART)
    * Idle: Tx=Hi-Z
    * Ustaw TIMER na one-pulse mode
    * wyłącz przerwania, sprawdza input, Tx=0, włącz TIMER, włącz przerwania
    * Timer po czasie 1 Tbit ustawia Tx=1, po 1.1 Tbit w przerwaniu ustawia Tx=Hi-Z i przełącza wejście na nadsłuchiwanie IRQ
    * Timer po czasie 10 + rand w przerwaniu ponawia próbę
    * LUB, jeżeli już nie ma więcej prób, timer po czasie 10 w przerwaniu rozpoczyna transmisję
* Jeżeli pakiet przestał płynąć przez określony czas, następuje reset stanu.


## 2. Data Link Layer

* Format pakietu:
  ```
  |  1  |  1   |   len     |     4      |  1  |  1   |      ESC = 0xAA
  | ESC | MASK | DATA^MASK | CRC32^MASK | ESC | STOP |      STOP = 0xFF
  |   BEGIN    |        CONTENT         |    END     |      len = 0..249
  ```
* Sposób wyznaczania `MASK`:
  * Zrób mapę występowania symboli w `DATA` i `CRC32`
  * Jeżeli `0xAA` nie występuje, `MASK` = 0
  * Zaznacz w mapie `0x00` i `0x55`.
  * Wybierz niewystępujący symbol `x` (przez to, że len <= 249, taki symbol zawsze istnieje).
  * Wyznacz `MASK = x ^ 0xAA`
* Jeżeli wystąpił ponownie BEGIN, zakończ aktualny pakiet i rozpocznij nowy pakiet
* Jeżeli wystąpił STOP, zakończ pakiet i uznaj łącze za dostępne.
* Wszystkie bajty poza pakietem są ignorowane.
* Jeżeli urządzenie ma kilka pakietów do wysłania i łącznie nie przekraczają danego
  rozmiaru, to może wysłać jeden po drugim oddzielając je BEGIN, a END jest
  na końcu ostatniego pakietu.

## 3. Network Layer

* Urządzenie ma adres 8-bitowy ustalany statycznie przez użytkownika przy rejestracji nowego urządzenia w sieci.
* Urządzenie ma nadaną przez użytkownika nazwę (string UTF-8).
* Urządzenie ma zmiennej długości unikalne ID pochodzące z hardware (np. ID mikrokontrolera). Pierszy bajt oznacza producenta.
* Router ma mapę adresów przypisującą adresy do portów fizycznych, np. jeżeli router
  ma dwa porty 1 i 2, to mapa posiada wartości:
  * 0 - urządzenie nie jest znane
  * 1 - urządzenie jest dostępne na porcie 1
  * 2 - urządzenie jest dostępne na porcie 2
  * Może to być zapisane na 2 bitach, więc mapa = 256 * 2 bity = 64 bajty.
  * Lub kompresując do 51 bajtów, gdzie jeden bajt opisuje 5 adresów używając
    mod/div przez 3. Trzeba pominąć też adres 0x00.
* Każdy pakiet zaczyna się od adresu źródłowego i jednego lub więcej adresów docelowych.
* Jeżeli router otrzyma pakiet, to uzupełnia swoją mapę na podstawie adresu źródłowego.
* Jeżeli adres docelowy nie jest znany, pakiet zostanie rozesłany na wszystkie porty oprócz tego, z którego przyszedł.
* Mapa może zostać zapisana do nieulotnej pamięci, jeżeli została zmieniona.
* Adres docelowy `0x00` to nowe urządzenie bez nadanego adresu.
* Router zawsze traktuje adres `0x00` jako nieznany (robi broadcast).
* Adres `0xAA` powinien być unikany, żeby zmniejszyć prawdopodobieństwo
  konieczności kodowania paketu w niższej warstwie. Jednak nie jest zabroniony.
* Pusta lista adresów docelowych oznacza pakiet broadcast.
* Urządzenie po starcie wysyła pusty pakiet broadcast, żeby zarejestrować się w 
  sieci. Może to być kilka pakietów w losowych odstępach czasu. Jeżeli urządzenie
  przez dłuższy czas nie wysyłało żadnych pakietów broadcast, to wysyła taki pakiet ponownie.
* Ramka
  ```
  |   1   |  1  | 0..N  |  M   |
  | FLAGS | SRC | DST[] | DATA |

  FLAGS:
     |   7  |  6 5 4   |  3 2 1 0  |
     | zero | PROTOCOL | DST_COUNT |
  0 - discovery protocol
  ```

### Discovery Protocol

* DISCOVERY: Sprawdzenie obecności urządzeń:
  * Użytkownik wysyła pakiet DISCOVERY (broadcast).
  * Każde urządzenie odpowiada pakietem DISCOVERY_RESPONSE po
    losowym czasie od 0 do 5 sekund.
  * Router też odpowiada.
  * Jeżeli router przekazuje pakiet DISCOVERY_RESPONSE, to dodaje swój adres do listy.
  * Dodany wpis routera zawiera adres i numer portu.
  * Jeżeli router jest źródłem pakietu DISCOVERY_RESPONSE, to nie dodaje swojego adresu.
  * Odpowiedź zawiera numer portu, z którego przyszło DISCOVERY. Dla urządzenia
    końcowego jest to zawsze 0.
  * Do odpowiedzi dołącza swoje ID i nazwę.
  * Aplikacja użytkownika powinna zrobić discovery kilka razy, żeby zredukować 
    prawdopodobieństwo utraty pakietu.
  * Tylko jeden proces discovery powinien być aktywny w danym czasie.
  ```
  DISCOVERY (broadcast):
    |    1     |   1   |
    | Type = 0 | flags |
    flags:
      bit 0: response with broadcast
      bit 1: forget routing map (just for routers)

  DISCOVERY_RESPONSE (unicast to SRC or broadcast):
    |    1     |  1   |  1    | ID len |    1     | namelen |   2 * N   |
    | Type = 1 | port |ID len |   ID   | Name len |  NAME   | routers[] |
  ```

* SET_ADDRESS: Nadanie adresu nowemu urządzeniu (lub zmiana istniejącemu):
  * Użytkownik wysyła pakiet SET_ADDRESS (broadcast) z ID i przydzielonym adresem.
  * Urządzenie z danym ID ustawia swój adres na podany.
  * Odpowiada, że się udało, jeżeli nie odpowie, to trzeba spróbować ponownie.
  * Odpowiedź posiada już nowy adres w SRC.
  * Po zmianie adresu, aplikacja powinna zrobić discovery, z flagami "forget routing map" i "response with broadcast", żeby zaktualizować mapy w routerach.
  ```
  SET_ADDRESS (broadcast):
    |     1      |    1    |   1    | ID len |
    | Type = 2   | address | ID len |   ID   |

  SET_ADDRESS_RESPONSE (unicast to SRC):
    |    1     |    1    |   1    | ID len |
    | Type = 3 | address | ID len |   ID   |
  ```

* SET_NAME: Zmiana nazwy urządzenia:
  * Użytkownik wysyła pakiet SET_NAME (unicast) z nową nazwą.
  * Odpowiada, że się udało, jeżeli nie odpowie, to trzeba spróbować ponownie.
  ```
  SET_NAME (unicast):
    |     1      |      1    | name len |
    | Type = 4   |  name len |   NAME   |

  SET_NAME_RESPONSE (unicast to SRC):
    |    1     |      1    | name len |
    | Type = 5 |  name len |   NAME   |
  ```

# <s>OLD STUFF</s>

### 3. Network Layer

* Urządzenie ma dwa adresy 8-bitowe:
  * Adres fizyczny - ustalany statycznie przez automat, który jest wywoływany przez użytkownika przy rejestracji nowego urządzenia w sieci.
  * Adres logiczny - ustalany statycznie przez użytkownika przy rejestracji urządzenia w sieci.
  * W kodzie używany jest adres logiczny, istnieje potokół podobny do ARP do mapowania adresów logicznych na fizyczne.
  * W paczce używany jest adres fizyczny.
  * Adres fizyczny używany jest do routingu pakietów typu unicast lub multicast.
* Routing:
  * Jedna z szyn jest oznaczana jako "główna" (main).
  * Router dla każdego portu ma prefiks: adres i liczba najstarszych bitów
  * Dla portu skierowanego na "główną" szynę (bezpośrednio lub pośrednio) prefiks to 0/0 i jest sprawdzany jako ostatni.
* Przyznawanie adresu fizycznego:
  * Automat do przyznawania najpierw wykrywa topologię sieci przy pomocy pakietów DISCOVER
  * Tworzy drzewo sieci, gdzie korzeń to szyna główna
  * Od szyny odgałęziają się urządzania końcowe, routery i kolejne szyny
  * Każdy liść (urządzenie końcowe albo router) rezerwuje 0 bitów młodszych
  * Każdy węzeł (szyna) rezerwuje minimalną liczbę bitów młodszych, tak aby pomieścić wszystkie swoje dzieci:
    * bierze parę z najmniejszą liczbą bitów i łączy je:
      * dopisuje zera na początek do elementu z mniejszą liczbą bitów, żeby liczba bitów była równa
      * dopisuje 0 do jednej strony i 1 do drugiej
      * teraz para jest reprezentowana przez jeden element z liczbą bitów powiększoną o 1 i może się łączyć z innymi elementami
    * powtarza, aż zostanie jeden element
  * w ten sposób przyznane adresy wszystkich elementów w tej szynie (łącznie z podszynach)
  * Kolejne bity dopisane w późniejszym czasie staną się prefiksem tej szyny,
    który zostanie zapisany w tablicy routingu routera łączącego tą szynę
    z nadrzędną
  * Po przeprocesowaniu szyny głównej, wszystkie adresy zostają przyznane

```
| 1 |  1  |  1 |
|  SRC |  DST  | TYPE |


DISCOVERY:
    src: -
    dst: broadcast

DISCOVERY_RESPONSE:
    src: -
    dst: broadcast
    data: id, addresses[]

SET_ADDRESS:
    src: -
    dst: broadcast
    data: id, address

SET_NAME:
    src: -
    dst: address
    data: name

SET_ROUTING:
    src: -
    dst: address
    data: routing_table[]
        prefix: 11 bits
        side: 1 bit: 0-receiving side, 1-other side
        length: 4 bits

```

### Protokół niskopoziomowy

* Wszystkie urządzenia są równorzędne
* Urządzenie domyśnie jest w stanie nadsłuchiwania UART
* Łącze uznane jest za dostępne, jeżeli ostatni pakiet się zakończył lub nic nie jest transmitowane przed odpowiedni czas (w przypadku, gdy nie jest znany ostani pakiet)
* Jeżeli urządzenie chce nadawać, czeka losowy czas (rozkład zależny od długości kolejki, czasu oczekiwania pierwszego pakietu w kolejce i najwyższego priorytetu pakietów z kolejki)
  * Przechodzi to trybu GPIO
  * Ustawia DataOutput=0
  * Sprawdza ostani raz, czy nikt inny nie zaczął nadawania (stan niski)
  * Ustawia TxEnable=1
  * Czeka 1 Tbit
  * Ustawia DataOutput=1
  * Czeka ~0.25 Tbit
  * Ustawia TxEnable=0
  * Czeka 8.75 Tbit
  * Czeka losowy czas 0..10Tbit (włączając czasy ułamkowe)
  * Powtarza jeszcze 2 razy
  * Jeżeli w tym czasie wykryto, że ktoś inny chce nadawać zaprzestajemy nadawania, przechodzimy do trybu UART i czekamy określony czas zanim znowu uznamy łącze za dostępne.
  * Przechodzi do trybu UART, TxEnable=1
  * ```
      <-- 10 --><- 0..10 -><-- 10 --><- 0..10 -><-- 10 -->
    -- ---------........... ---------........... ---------DDDDDDDDDDD
      -                    -                    -         DDDDDDDDDDD
    ```
  * Przesyłamy pakiet
  * Przechodzi do trybu UART, TxEnable=0
  * Praktyczne podejście na STM32:
    * (RS-485)
    * Ustaw TIMER na one-pulse mode
    * DataOut = 0, wyłącz przerwania, sprawdza input, TxEn = 1, włącz TIMER, włącz przerwania
    * Timer po czasie 1 Tbit ustawia DataOut = 1, po 1.25 Tbit w przerwaniu ustawia TxEn = 0 i przełącza wejście na nadsłuchiwanie IRQ
    * Timer po czasie 10 + rand w przerwaniu ponawia próbę
    * LUB, jeżeli już nie ma więcej prób, timer po czasie 10 w przerwaniu rozpoczyna transmisję
    * (1-wire UART)
    * Idle: Tx=Hi-Z
    * Ustaw TIMER na one-pulse mode
    * wyłącz przerwania, sprawdza input, Tx=0, włącz TIMER, włącz przerwania
    * Timer po czasie 1 Tbit ustawia Tx=1, po 1.1 Tbit w przerwaniu ustawia Tx=Hi-Z i przełącza wejście na nadsłuchiwanie IRQ
    * Timer po czasie 10 + rand w przerwaniu ponawia próbę
    * LUB, jeżeli już nie ma więcej prób, timer po czasie 10 w przerwaniu rozpoczyna transmisję
    * 
* Format pakietu:
  ```
  |  1  |  1  |   len    |     4     |  1  |  1   |      ESC = 0xAA
  | ESC | XOR | DATA^XOR | CRC32^XOR | ESC | STOP |      STOP = 0xFF
  |   BEGIN   |       CONTENT        |    END     |      len = 0..249
  ```
  Sposób wyznaczania `XOR`:
  * Zrób mapę występowania symboli w `DATA` i `CRC32`
  * Jeżeli `0xAA` nie występuje, `XOR` = 0
  * Zaznacz w mapie `0x00` i `0x55`.
  * Wybierz niewystępujący symbol `x` (przez to, że len <= 249, taki symbol zawsze istnieje).
  * Wyznacz `XOR = x ^ 0xAA`
* Jeżeli pakiet przestał płynąć przez określony czas, następuje reset stanu.
* Jeżeli wystąpił ponownie BEGIN, rozpocznij nowy pakiet ignorując poprzedni
* Jeżeli wystąpił STOP, zakończ pakiet i uznaj łącze za dostępne.
* Ogólny overhead:
  * 5 bajtów na żądanie łącza
  * 4 bajty na BEGIN, END
  * 4 bajty na CRC32
  * 7 bajtów na odstęp między pakietami
  * 20 RAZEM bajtów / pakiet

### Protokół na wyższym poziomie

* Adres jest 12 bitowy
* Adresy i routowanie jest ustalane statycznie (np. przez interface www)
* Każde urządzenie posiada unikatowy ID, który zaczyna się od bajtu producenta i unikalnego ID nadanego przez producenta chipa
* Każde urządzenie zapamiętuje swój adres i string z jego nazwą (UTF-8)
* Nowe urządzenia mają adres 0 i są specyjalnie traktowane (może być kilka z takim adresem)
* Topologia sieci rozpoznawana jest przez wysłanie specyjanego pakietu brodcast DISCOVERY
  * Można wysłać kilka takich pakietów, żeby mieć pewność, że nie został zgubiony (albo jakaś odpowiedź na niego)
  * W odpowiedzi każde urządzenie odsyła swój adres, ID
  * Zapytanie o nazwę urządzenia odbywa się przez pakiet bezpośredni, żeby nie opciąrzać odpowiedzi DISCOVERY
  * Jeżeli taka odpowiedź przechodzi przez GW, to on dodaje swój adres
  * Urządzenia z adresem 0 też odsyłają taką odpowiedź
  * Na bazie topologi sieci można zmieniać adresy, nazwy i prefixy routowania dla GW
  * Urządzenia z adresem 0 są konfigurowane przez wysłanie pakietu brodcast zawierającego ID urządzenia (zamiast adresu)
* GW routuje pakiety na bazie prefixu, można zdefiniować, że dany prefix (lub kilka) jest po określonej stronie GW i wtedy GW nie rozsyła dalej pakietów jeżeli nie potrzeba
