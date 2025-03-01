# projekt-so
Temat projektu - "Egzamin"

TREŚĆ POLECENIA

Na wydziale X pewnej uczelni studiują studenci na K kierunkach (K>=5). Na każdym z kierunków studiuje N_i (80<=N_i<=160) studentów. 
Na wydziale ogłoszono egzamin dla studentów 2-ego roku, ale zapomniano podać nazwy kierunku. W związku z tym w danym dniu przed budynkiem wydziału zgromadzili się studenci z wszystkich K kierunków czekając w kolejce na wejście (studenci pojawiają się w losowych momentach czasu i ich liczba też jest losowana z podanego zakresu). Do zgromadzonych studentów dziekan wysłał wiadomość, że egzamin dotyczy studentów kierunku i (np.: kierunek 2). Studenci wskazanego kierunku wchodzą do budynku, pozostali studenci wracają do domu. 
Egzamin składa się z 2 części: części praktycznej (komisja A) i części teoretycznej (komisja B). Każda komisja składa się z 3 osób i przyjmuje w osobnym pokoju. Każda z osób w komisji zadaje po jednym pytaniu, pytania są przygotowywane na bieżąco (co losową liczbę sekund) w trakcie egzaminu. Może zdarzyć się sytuacja w której, członek komisji spóźnia się z zadaniem pytania wówczas student czeka aż otrzyma wszystkie 3 pytania. Po otrzymaniu pytań student ma określony czas T na przygotowanie się do odpowiedzi. Po tym czasie student udziela komisji odpowiedzi (jeżeli w tym czasie inny student siedzi przed komisją, musi zaczekać aż zwolni się miejsce), które są oceniane przez osobę w komisji, która zadała dane pytanie. Przewodniczący komisji (jedna z trzech osób) ustala ocenę końcową z danej części egzaminu (średnia arytmetyczna zaokrąglona w górę do skali ocen 5.0, 4.5, 4.0, 3.5, 3.0, 2.0). 
Do danej komisji mogą wejść jednocześnie maksymalnie 3 osoby. Zakładamy, że każdą z części egzaminu na ocenę pozytywną zdaje 95% studentów (ocena za każdą odpowiedź jest losowana, jeżeli student otrzymał przynajmniej jedną ocenę 2.0 nie zdał danej części egzaminu). Zasady przeprowadzania egzaminu:
• Studenci w pierwszej kolejności zdają egzamin praktyczny. 
• Jeżeli student nie zdał części praktycznej nie podchodzi do części teoretycznej. 
• Po pozytywnym zaliczeniu części praktycznej student staje w kolejce do komisji B. 
• Wśród studentów znajdują się osoby powtarzające egzamin, które mają już zaliczoną część praktyczną egzaminu (ok. 5% studentów) – takie osoby informują komisję A, że mają zaliczenie i zdają tylko część teoretyczną. 
• Ocenę końcową z zaliczenia egzaminu wystawia Dziekan po pozytywnym zaliczeniu obu części egzaminu – dane do Dziekana przesyłają przewodniczący komisji A i B. 
• Po wyjściu ostatniego studenta Dziekan publikuje listę studentów (id studenta) z otrzymanymi ocenami w komisji A i B oraz oceną końcową z egzaminu. 
Na komunikat (sygnał1) o ewakuacji – sygnał wysyła Dziekan - studenci natychmiast przerywają egzamin i opuszczają budynek wydziału – Dziekan publikuje listę studentów z ocenami, którzy wzięli udział w egzaminie. 
Napisz programy Dziekan, Komisja i Student symulujące przeprowadzenie egzaminu.
