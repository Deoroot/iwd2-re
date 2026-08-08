# Apprendre à *recover* une fonction — tutoriel sur un cas réel

> Cas d'étude : `IcewindCProjectileTravellingVFX::Fire` à l'adresse `0x5791D0`
> (commit `fe1dc071`). C'est la fonction qui a corrigé le bug visuel de Color
> Spray et de tous les sorts en cône.
>
> Ce document est écrit pour quelqu'un qui débute en C++, qui ne connaît
> presque pas l'assembleur, et pas du tout Ghidra. On définit chaque mot de
> jargon **avant** de s'en servir. Prends ton temps sur la partie « Vocabulaire »,
> tout le reste en dépend.

---

## 0. C'est quoi « recover » ici ?

Le jeu **Icewind Dale 2** est livré sous forme d'un `.exe` : un fichier de
**code machine** (des octets que le processeur exécute, illisibles pour un
humain). Il n'existe **pas** de code source original.

Notre projet reconstruit, fonction par fonction, le **code source C++** d'origine,
de façon à ce que, une fois recompilé, il produise *le même comportement* que
`IWD2.exe`. « Recover une fonction » = retrouver le C++ d'une seule fonction.

On ne **devine** jamais. On a plusieurs façons de regarder ce que fait vraiment
le binaire, classées par fiabilité (voir §2). Le métier, c'est de traduire ce
qu'on observe en C++ propre et fidèle.

---

## 1. Vocabulaire indispensable

### 1.1 Classe, objet, membre, méthode

En C++, une **classe** est un plan de construction. Exemple simplifié :

```cpp
class CProjectile {            // le plan
    LONG  m_sourceId;         // un MEMBRE (une variable rangée dans l'objet)
    LONG  m_targetId;         // un autre membre
    void  Fire(...);          // une MÉTHODE (une fonction qui agit sur l'objet)
};
```

- Un **objet** (ou « instance ») est un exemplaire concret fabriqué à partir du
  plan. Si la classe est « plan de maison », l'objet est « la maison construite ».
- Un **membre** (`m_sourceId`, `m_targetId`…) est une variable qui vit *à
  l'intérieur* de l'objet. Le préfixe `m_` veut juste dire « member ».
- Une **méthode** est une fonction attachée à la classe. `Fire` est une méthode.

Dans l'objet, les membres sont rangés les uns après les autres en mémoire. La
distance en octets entre le début de l'objet et un membre s'appelle un
**offset** (décalage). Exemple : `m_targetId` est à l'offset `+0x76`, c'est-à-dire
118 octets après le début de l'objet. Retiens ce mot **offset**, il revient
partout.

### 1.2 Constructeur (« ctor »)

Un **constructeur** est une méthode spéciale qui s'exécute automatiquement au
**moment où l'objet est créé**. Son rôle : initialiser l'objet (mettre les
membres à leurs valeurs de départ). On l'abrège souvent **ctor**.

Détail crucial pour nous : le constructeur écrit aussi, tout au début de l'objet,
un pointeur vers la **vtable** (voir juste en dessous). C'est pour ça qu'on
inspecte les constructeurs quand on cherche des bugs de classe.

### 1.3 Méthode virtuelle, vtable, slot

Une **méthode virtuelle** est une méthode dont la *vraie version exécutée* est
choisie **au moment de l'exécution**, selon le type réel de l'objet.

Exemple : `CProjectile` a une méthode virtuelle `Fire`. Une sous-classe comme
`IcewindCProjectileTravellingVFX` peut fournir **sa propre** version de `Fire`.
Quand le jeu appelle `monProjectile->Fire(...)`, c'est la version correspondant
au *vrai* type de `monProjectile` qui part, pas forcément celle de la classe de
base.

Comment le binaire choisit ? Avec une **vtable** (table de méthodes virtuelles).
C'est un simple **tableau d'adresses de fonctions**. Chaque case du tableau
s'appelle un **slot**. Le slot 0 contient l'adresse de la 1ʳᵉ méthode virtuelle,
le slot 1 la suivante, etc. Chaque slot fait 4 octets (une adresse 32 bits).

```
vtable de la famille (adresse 0x850D54)
┌──────┬──────────┬────────────┬──────────────────────────────────────────┐
│ slot │ offset   │ adresse    │ méthode                                    │
├──────┼──────────┼────────────┼────────────────────────────────────────────┤
│  0   │ +0x000   │ 0x00579b10 │ ~CProjectileConePulseVisual (destructeur)  │
│  1   │ +0x004   │ 0x0045b680 │ CGameTimer::GetId                          │
│  2   │ +0x008   │ 0x004c7be0 │ CGameObject::AddToArea                     │
│  3   │ +0x00c   │ 0x00578ab0 │ IcewindCProjectileTravellingVFX::AIUpdate  │
│ ...  │   ...    │    ...     │ ...                                        │
│ 27   │ +0x06c   │ 0x005791d0 │ IcewindCProjectileTravellingVFX::Fire ◄────┼── NOTRE fonction
└──────┴──────────┴────────────┴────────────────────────────────────────────┘
```

> Calcul du slot : `offset = slot × 4`. Slot 27 → `27 × 4 = 0x6C`. L'adresse de la
> vtable + `0x6C` = `0x850DC0`, et on a vérifié que les 4 octets rangés là
> valent bien `0x005791D0`. Donc « **slot 27 de la vtable propriétaire** » =
> « la 28ᵉ méthode virtuelle de cette famille de classes ».

**Pourquoi ça compte pour nous (le piège de la conflation) :** Ghidra peut
fusionner par erreur deux classes binaires distinctes en une seule dans notre
source (parce qu'elles se ressemblent). Si ça arrive, on risque de recoder *la
mauvaise* méthode virtuelle. Le garde-fou : toujours vérifier qu'une méthode
virtuelle est bien à **son** slot dans **sa** vtable, et non se fier
aveuglément au nom que Ghidra lui a collé. (C'est exactement ce bug qui avait
rendu une Fireball verte par le passé.)

### 1.4 `this`, `__thiscall`, et les registres

Quand on appelle une méthode sur un objet — `monObjet->Fire(a, b)` — le
compilateur passe **secrètement** l'adresse de l'objet comme tout premier
paramètre caché. Ce paramètre s'appelle **`this`**. C'est ce qui permet à `Fire`
de savoir *sur quel objet* il travaille (pour lire/écrire ses membres).

Sur Windows 32 bits, la convention utilisée pour passer `this` s'appelle
**`__thiscall`** : `this` est mis dans un **registre** nommé **`ecx`**.

Un **registre** est une mini-variable ultra-rapide intégrée au processeur. Il y
en a peu (`eax`, `ebx`, `ecx`, `edx`, `esi`, `edi`, `esp`, `ebp`). Deux à
retenir :
- **`ecx`** : contient `this` au début d'une méthode `__thiscall`.
- **`esp`** : le « pointeur de pile » ; les autres arguments (a, b…) sont rangés
  sur la **pile** (la mémoire temporaire de la fonction), qu'on lit via `esp`.

Donc, quand tu liras dans l'assembleur :

```asm
mov  esi, ecx          ; copie ecx (= this) dans esi pour le garder sous la main
```

…ça veut dire « range `this` dans `esi` ». À partir de là, `esi` = notre objet.

### 1.5 Lire 4 lignes d'assembleur

L'**assembleur** (asm) est le code machine rendu lisible : une instruction par
ligne. Tu n'as pas besoin de tout maîtriser, juste ce motif :

| Instruction asm        | Traduction en français                                            |
|------------------------|-------------------------------------------------------------------|
| `mov A, B`             | copie la valeur `B` dans `A`                                       |
| `[esi + 0x76]`         | « la mémoire à l'adresse `esi+0x76` » → le **membre à l'offset 0x76** |
| `mov [esi+0x76], ebp`  | écrit le contenu de `ebp` dans le membre `+0x76` de l'objet        |
| `cmp X, Y`             | compare `X` et `Y` (prépare un saut conditionnel)                  |
| `je  CIBLE`            | « jump if equal » : saute vers `CIBLE` si la comparaison était égale |
| `push X` / `pop`       | empile / dépile une valeur (gestion de la pile)                   |
| `call CIBLE`           | appelle la fonction à l'adresse `CIBLE`                            |

Les crochets `[...]` = « va lire/écrire en mémoire à cette adresse ». Sans
crochets, on manipule la valeur directement.

### 1.6 Ghidra et le « décompile »

**Ghidra** est un outil qui lit le `.exe` et tente de reconstruire un
pseudo-C lisible à partir du code machine : c'est le **décompile**. C'est très
pratique pour *naviguer*, **mais c'est une supposition faillible** : Ghidra
invente des types, mélange les arguments, se trompe sur les tailles (16 vs 32
bits), etc.

Quand Ghidra n'arrive pas à nommer ou typer quelque chose, il met des
étiquettes génériques. Voici le dictionnaire :

| Ce que Ghidra écrit  | Ce que ça veut dire                                                      |
|----------------------|-------------------------------------------------------------------------|
| `FUN_005791d0`       | « une fonction trouvée à l'adresse 0x5791d0, sans nom connu »            |
| `DAT_00851878`       | « une variable globale à l'adresse 0x851878, sans nom connu »            |
| `param_1`, `param_2` | « le 1ᵉʳ, 2ᵉ… paramètre » (Ghidra n'a pas su comment l'appeler)          |
| `local_64`, `uStack_88` | des variables locales auto-nommées (le nombre ≈ leur position en pile) |
| `undefined`          | « type inconnu, taille inconnue »                                        |
| `undefined4`         | « type inconnu de **4** octets » (le chiffre = la taille en octets)     |
| `undefined2`         | type inconnu de 2 octets ; `char` = 1 octet ; `short` = 2 ; `int` = 4   |
| `(int)param_1 + 0x76`| « l'objet, + 0x76 octets » → un accès à un membre, mais Ghidra ne connaît pas son nom |

Tu verras aussi, en haut de presque chaque fonction, un bloc comme :

```c
puStack_8 = &LAB_00820736;
local_c = ExceptionList;
ExceptionList = &local_c;        // ... et plus loin :  ExceptionList = local_c;
```

et en asm `mov eax, dword ptr fs:[0]`. C'est de la **plomberie d'exceptions
Windows** (SEH). C'est du passe-partout généré par le compilateur ; **tu peux
l'ignorer** pour comprendre la logique. Ne te laisse pas effrayer par ces lignes.

### 1.7 Les autres outils nommés dans ce tuto

- **Frida** : un outil qui s'« accroche » au jeu **pendant qu'il tourne** et qui
  journalise ce qui se passe réellement (vrais arguments, quelle branche du code
  s'exécute, contenu des champs…). C'est notre vérité « dynamique ». On l'utilise
  surtout sur l'`IWD2.exe` original (qui n'a pas de source) et pour comparer
  original vs notre build.
- **`gb`** : raccourci pour notre pont vers Ghidra (`ghidra-bridge`). Par exemple
  `gb unimplemented CNomDeClasse` liste les méthodes d'une classe qu'on **n'a pas
  encore recovrées** (elles sont encore des coquilles vides / « stubs »). C'est
  une façon de **trouver des cibles** à recover.
- **`parity`** : un « linter de fidélité ». Il compare automatiquement notre
  source au binaire et donne un verdict **GREEN / YELLOW / RED**. Détaillé en §
  Phase 6.
- **stub** : une fonction *coquille vide*, pas encore recovrée. Soit elle ne fait
  rien, soit elle délègue à une autre fonction en attendant. Notre `Fire` **était
  un stub** avant ce commit.

---

## 2. Le principe directeur : la hiérarchie de vérité

C'est **LA** chose à retenir. Quand deux sources d'info se contredisent, on croit
la plus haute :

| Rang | Source | Sert de référence pour…                                                        |
|------|--------|-------------------------------------------------------------------------------|
| 1 (vérité ultime) | **Octets / assembleur** du `.exe` | les instructions exactes, les cibles d'appel, les offsets de membres |
| 2 | **Trace Frida** (jeu qui tourne) | les vrais arguments, quelle branche s'exécute, le sens d'un champ/drapeau |
| 3 (brouillon) | **Décompile Ghidra** | une première lecture pour *naviguer* — à **vérifier** avant de croire les compteurs, types, largeurs, offsets |

> On **navigue** avec le décompile (rang 3), mais on **tranche** avec l'asm
> (rang 1) ou Frida (rang 2). Recoder uniquement à partir du décompile = inventer.

---

## 3. Les phases du recover (sur notre cas réel)

### Phase 0 — Choisir la cible

Notre fonction n'est pas tombée du ciel. Le point de départ était un **bug
visuel** : Color Spray (et tous les sorts en cône) affichait ses particules
décalées par rapport au lanceur.

Premier réflexe : regarder *qui appelle* la zone suspecte. L'outil `fn_digest.py`
donne un résumé rapide et **gratuit** (en tokens) d'une fonction :

```bash
python3 scripts/fn_digest.py 0x5791D0
```

```
callers (4): FUN_00579ef0, FUN_00580b30, CProjectileWhirlwind::Fire, CProjectileCone::Pulse
```

« callers » = les fonctions qui **appellent** celle-ci. On y voit `CProjectileCone`
et `CProjectileWhirlwind` → c'est bien sur le chemin des cônes. Et en lisant la
fonction, on découvre qu'elle était un **stub** qui déléguait à la classe de
base. Un stub, appelé par un chemin buggé : **cible idéale**.

Autres façons classiques de trouver une cible :
```bash
gb unimplemented CProjectileTravelling   # liste les méthodes encore non recovrées de cette classe
```
ou : suivre les « trous » (une fonction recovrée qui appelle une fonction encore
stub), ou prendre les fonctions les plus appelées.

### Phase 1 — Prouver OÙ est le bug *avant* d'écrire une ligne

C'est l'habitude la plus importante du métier. **On ne devine pas où est le
bug ; on le prouve.**

Le décompile laissait penser que le problème pouvait être dans l'« émission »
(le code qui crée les particules). Au lieu de le croire, on a fait une trace
**différentielle** avec Frida :

- on lance l'**original** `IWD2.exe` avec des sondes (« hooks ») qui journalisent
  l'émission ;
- on lance **notre build** avec **exactement les mêmes** sondes ;
- on **compare** les deux journaux.

```bash
scripts/vm.sh frida scripts/probes/frida_colorspray_trace.py
```

Résultat : émission **identique octet pour octet** (21 visuels, vitesse 20,
3 pulsations) entre l'original et notre build. Donc l'émission **n'est pas** le
bug. Cela a *éliminé* une grosse zone et *pointé* le doigt sur la « feuille »
finale : le lancement (`Fire`).

> Sans cette preuve, on aurait pu « réparer » l'émission pendant des jours alors
> qu'elle était déjà correcte. Une comparaison original-vs-notre-build est un
> **oracle** : elle dit où chercher.

### Phase 2 — Vérifier le slot de vtable (anti-conflation)

`Fire` est une **méthode virtuelle**. Avant de la toucher, on confirme qu'on
recode bien *la bonne* : elle doit être au **slot 27 de la vtable de sa
famille** (adresse `0x850D54`), et non un homonyme.

```bash
.venv-reagent/bin/python scripts/sym.py vtable 0x850D54     # affiche les slots
.venv-reagent/bin/python scripts/sym.py u32 0x850DC0        # lit le slot 27 (0x850D54 + 27*4)
# -> 0x00850dc0: 0x005791d0   ; IcewindCProjectileTravellingVFX::Fire   ✅

python scripts/ctor_vtable_check.py CProjectileTravelling    # alerte si des ctors posent des vtables différentes (= conflation)
python scripts/vtable_audit.py IcewindCProjectileTravellingVFX
```

`ctor_vtable_check` regarde les **constructeurs** (cf. §1.2) : si deux
constructeurs de ce qui semble « une seule classe » installent **deux vtables
différentes**, c'est le signe que Ghidra a fusionné deux classes binaires → il
faut démêler avant de recover, sinon on recode la mauvaise méthode.

### Phase 3 — Assembler le contexte AVANT d'écrire

On ne recode jamais depuis un décompile nu. On rassemble d'abord, hors-ligne, un
« paquet de contexte » :

```bash
python scripts/reagent_assemble_context.py --address 0x5791D0 --out tmp_ctx.md
```

Ce paquet contient :
- le décompile **résolu** (avec nos vrais noms à la place des `FUN_`/`DAT_`, et
  les appels virtuels annotés de leur n° de slot) ;
- le **REQUIRED CALL SET** : la liste, tirée du binaire, des fonctions que
  `Fire` **doit** appeler, dans l'ordre → c'est de la vérité de rang 1, à
  reproduire exactement ;
- la disposition mémoire de la classe (offsets des membres) ;
- les constantes nommées (les valeurs « magiques » remplacées par leur nom) ;
- l'en-tête (`.h`) de la classe.

On **lit** ce paquet, **puis** on écrit.

### Phase 4 — Lire dans la hiérarchie de vérité (3 vues du même code)

Voici le cœur pédagogique. Prenons **les toutes premières lignes** de la
fonction et regardons-les dans les **trois** vues. C'est exactement le même bout
de programme, vu de trois hauteurs différentes.

**Vue 3 — décompile Ghidra (brouillon, peu lisible) :**

```c
void __thiscall
FUN_005791d0(undefined4 *param_1, undefined4 param_2, undefined4 param_3,
             undefined4 **param_4, int param_5, int param_6)
{
  ...
  *(undefined4 ***)((int)param_1 + 0x76) = param_4;   // écrit param_4 à l'offset 0x76 de l'objet
  *(undefined4   *)((int)param_1 + 0x72) = param_3;   // param_3 à l'offset 0x72
  *(undefined4   *)((int)param_1 + 0xea) = param_2;   // param_2 à l'offset 0xea
  if (param_4 == (undefined4 **)DAT_00851878) {        // si param_4 == une certaine globale
    ...
```

Remarques : la **signature** affichée par Ghidra était même
`undefined FUN_005791d0(void)` (« fonction sans nom, sans arguments, type
inconnu »). Le mot **`__thiscall`** te dit que le 1ᵉʳ paramètre, `param_1`, est en
réalité `this` (l'objet). Les `param_2/3/4` sont les vrais arguments, mais Ghidra
ne sait pas les nommer ni les typer. `+0x76`, `+0x72`, `+0xea` sont des **offsets
de membres** dont Ghidra ignore les noms.

**Vue 1 — assembleur (vérité ultime) :**

```asm
0x005791f3: mov  esi, ecx              ; esi <- ecx : ecx contient "this" (convention __thiscall)
0x005791f9: mov  [esi + 0x76], ebp     ; objet->(+0x76) = ebp   (ebp = l'argument "target")
0x005791fc: mov  [esi + 0x72], eax     ; objet->(+0x72) = eax   (eax = l'argument "source")
0x005791ff: mov  [esi + 0xea], ecx     ; objet->(+0xea) = ecx   (ecx = l'argument "pArea")
0x00579205: cmp  ebp, [0x851878]       ; compare "target" à la globale 0x851878
0x0057920c: je   0x579371              ; si égal -> saute vers la branche "cible = un point"
```

L'asm confirme tout sans ambiguïté : `this` arrive bien dans `ecx` (puis rangé
dans `esi`), et trois membres aux offsets `0x76 / 0x72 / 0xea` reçoivent les
trois arguments. La comparaison + le saut (`cmp` puis `je`) est un simple
`if`.

**Vue 0 — notre C++ final (lisible, fidèle) :**

```cpp
void IcewindCProjectileTravellingVFX::Fire(CGameArea* pArea, LONG source, LONG target,
                                           CPoint targetPos, LONG nHeight, SHORT nType)
{
    m_targetId = target;      // l'offset 0x76 a un nom : m_targetId
    m_sourceId = source;      // l'offset 0x72 = m_sourceId
    m_pArea    = pArea;       // l'offset 0xea = m_pArea

    CPoint ptTarget;
    BOOL bTargetCreature = FALSE;
    if (m_targetId == CGameObjectArray::INVALID_INDEX) {   // la globale 0x851878 = INVALID_INDEX
        ptTarget = targetPos;                              // branche "cible = un point"
    } else {
        ...
```

Conclusion : la globale mystérieuse `DAT_00851878` n'était autre que la constante
nommée `CGameObjectArray::INVALID_INDEX`, et les offsets `0x76 / 0x72 / 0xea`
sont les membres `m_targetId / m_sourceId / m_pArea`. On a traduit du rang 1
(certain) vers du C++ lisible.

#### À quoi ressemble un appel de méthode **virtuelle** dans le décompile

Toujours dans la même fonction, Ghidra écrit :

```c
cVar1 = (**(code **)(*local_64 + 4))();
```

Ça paraît cryptique, mais ça se décode mécaniquement :
- `local_64` = un pointeur vers un objet (ici, la cible).
- `*local_64` = les 4 premiers octets de l'objet = **son pointeur de vtable**
  (rappel §1.3 : la vtable est posée tout au début de l'objet par le ctor).
- `+ 4` = avance de 4 octets dans la vtable = **slot 1** (chaque slot = 4 octets).
- `(**(code **) ... )()` = « appelle la fonction trouvée là ».

Donc cette ligne = « appelle la méthode virtuelle n° 1 de la cible ». Dans notre
code recovré, ça devient simplement :

```cpp
bTargetCreature = (pTarget->GetObjectType() == CGameObject::TYPE_SPRITE);
```

Voilà comment on passe d'un appel virtuel illisible à un appel de méthode normal.

### Phase 5 — Écrire le C++ idiomatique (et assumer les PARTIAL)

Une fois la logique comprise, on écrit du C++ qui *ressemble* au reste du code et
qui reproduit fidèlement le binaire.

**Le bug, en une idée :** l'ancien stub ajoutait le projectile **au niveau du
sol** (hauteur Z = 0), alors que la vraie fonction l'ajoute **à la hauteur de
lancement du sort** (la hauteur du corps du lanceur). En projection isométrique,
les pieds sont décalés vers le bas/avant des mains → d'où les particules
décalées.

Avant (stub) :

```cpp
// 0x5791D0 (vtable slot 27)
// UNIMPLEMENTED: delegate to the base launch until recovered.
void IcewindCProjectileTravellingVFX::Fire(...) {
    CProjectileTravelling::Fire(pArea, source, target, targetPos, nHeight, nType);
}
```

Après (extrait clé du recover) :

```cpp
    // hauteur de lancement = la hauteur de cast de la source
    nLaunchHeight = DetermineHeight(static_cast<CGameSprite*>(pSource));
    ...
    // on enregistre l'objet, PUIS on l'ajoute à la zone À LA HAUTEUR DE CAST
    // (la base, elle, l'ajoutait au sol : hauteur 0 -> d'où le bug visuel)
    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    AddToArea(pArea, ptLaunch, nLaunchHeight, 0);
```

**La règle « no hacks ».** Deux bouts de la fonction dépendaient de fonctions
qu'on n'a **pas encore** recovrées (la hauteur d'animation d'une créature ; un
objet de traînée créé via un type de message à l'adresse `0x84D328`). On
**n'invente pas** ce code. On le marque honnêtement :

```cpp
    // PARTIAL: a creature target's animation height rect (GetHeight) would be
    // read here to refine the launch delta-Z; the default rect is retained.
```

`// PARTIAL:` (ou `// HACK:` / laisser un stub) documente ce qui manque et
pourquoi. **Manquant vaut mieux que faux.** Ces deux PARTIAL ne sont pas sur le
chemin des cônes (qui visent un point, pas une créature), donc le bug visuel est
bien corrigé ; il reste juste deux raffinements pour plus tard.

### Phase 6 — Vérifier : d'abord la parité (statique), puis le runtime

#### 6.a — La parité (analyse *statique* : on lit le code, on ne le lance pas)

```bash
.venv-reagent/bin/re-agent --config re-agent.host.yaml parity --address 0x5791D0
```

La parité produit un verdict :
- **GREEN** = notre source colle au binaire ;
- **YELLOW** = différence *probablement bénigne* (à regarder, souvent un faux
  positif) ;
- **RED** = différence *suspecte* (probable erreur de logique à corriger).

Elle s'appuie sur 11 « signaux ». Le plus parlant — et celui sur lequel tu m'as
interrogé — est le **compteur d'appels** (« call count »).

#### 6.b — Pourquoi YELLOW affiche-t-il « une différence de call » ?

Un des signaux compte **combien d'appels de fonction** le binaire fait depuis
cette fonction, et le compare à **combien notre source en fait**. Si les deux
nombres diffèrent, le signal vire au YELLOW (ou RED). Exemple de message :

```
call count: binary 24 vs source 21   (YELLOW)
```

« Le binaire fait 24 appels, notre source seulement 21. » Première réaction de
débutant : « j'ai oublié 3 appels ! ». **Souvent, non.** Voici pourquoi c'est
fréquemment un **faux positif** :

- **L'inlining.** Pour aller plus vite, le compilateur **recopie parfois le corps
  d'une petite fonction directement chez l'appelant** au lieu d'émettre une
  instruction `call`. Les victimes habituelles : les helpers de la bibliothèque
  standard (STL) et les classes utilitaires — `std::vector`, `CString`
  (constructeur/destructeur de chaîne), `min`/`max`, petits accesseurs.
  - Notre source écrit `CString s = ...;` → ça *paraît* être 1 ou 2 appels
    (constructeur/destructeur). Mais dans le binaire, le compilateur les a
    **inlinés** → 0 `call` à cet endroit → le compte diffère, alors que le
    comportement est **identique**.
  - Le contraire arrive aussi : notre `sqrt(x)` = 1 appel dans la source, mais le
    binaire le transforme en plusieurs appels à des helpers flottants internes
    (`__ftol`, conversions) → le binaire a **plus** d'appels.
- Le compilateur peut aussi **fusionner**, **réordonner** des appels, ou une
  macro peut s'étendre en plusieurs appels.

Donc « binary 24 vs source 21 » signifie en général : 3 appels ont été
inlinés/transformés différemment — **pas** qu'on a perdu de la logique. La doc du
projet appelle ça littéralement des « classic false positives ».

**Comment trancher GREEN/réel vs faux positif ?** Regarder **quels** appels
diffèrent (la parité les liste) :
- si ce sont du `CString`, `std::vector`, des helpers maths (`__ftol`…), de
  petits getters → c'est du bruit d'inlining → **bénin**, on garde ;
- si c'est une **vraie** fonction de jeu qui manque (`AddToArea`, `GetShare`,
  `DetermineHeight`…) → là, c'est **réel** : on a oublié de la logique, à
  corriger.

> Note : certains signaux de parité doivent lire l'assembleur, ce qui démarre
> Ghidra en arrière-plan (~1 min par appel). C'est normal que ce soit lent.

Outils complémentaires quand la fonction touche beaucoup de membres :

```bash
.venv-reagent/bin/python scripts/parity_offsets.py 0x5791D0    # "bon callee, mauvais membre"
python scripts/struct_layout_audit.py IcewindCProjectileTravellingVFX
```

`parity_offsets` attrape le cas vicieux « on appelle la bonne fonction, le bon
nombre de fois, mais en lisant/écrivant le **mauvais membre** » (un offset à côté)
— que le compteur d'appels, lui, ne voit pas.

#### 6.c — Le runtime (la seule vraie preuve)

**Point capital : GREEN ne prouve PAS que c'est correct.** La parité est
*statique* : elle lit le code mais ne **lance** jamais le jeu. Elle est donc
**aveugle** aux bugs de disposition mémoire et aux bugs visuels (des bugs réels
sont déjà passés « GREEN » : une sur-densité d'anneau de Cloudkill, une teinte de
cadavre fausse pendant 3 ans).

L'arc n'est terminé que quand **notre build exécute vraiment le chemin** :

```bash
scripts/vm.sh build      # compile dans la VM (doit compiler sans erreur, VS2019 Win32)
scripts/vm.sh smoke      # lance notre exe + une sauvegarde + un détecteur de crash
```

`smoke` charge une partie et surveille les plantages. Et pour un **changement
visuel** comme ici, la preuve finale, c'est **l'œil de l'utilisateur** : tu lances
le sort et tu confirmes (« ça marche »). C'est ce qui a clos ce cas-ci.

### Phase 7 — Commit & suite

Commit (tu sais déjà ce que c'est) : le message documente surtout le **pourquoi**
et liste les **PARTIAL**. Puis on recommande la ou les prochaines cibles —
typiquement les fonctions qui lèveraient les PARTIAL restants.

---

## 4. Mémo (checklist à relire avant chaque recover)

```
0. CIBLE        callers (fn_digest) / gb unimplemented / stub sur un chemin buggé
1. PROUVER      où est le bug -> trace Frida différentielle (ne pas deviner)
2. VTABLE       méthode virtuelle ? vérifier le bon SLOT de la bonne vtable (anti-conflation)
3. CONTEXTE     reagent_assemble_context.py AVANT d'écrire (jamais depuis un décompile nu)
4. VÉRITÉ       asm/Frida tranchent ; le décompile ne sert qu'à naviguer
5. ÉCRIRE       C++ idiomatique + // PARTIAL honnête si un callee manque (no hacks)
6. VÉRIFIER     parity (GREEN/YELLOW) PUIS build + smoke + œil utilisateur
7. COMMIT       le POURQUOI + PARTIAL, puis nommer la cible suivante
```

**La seule habitude qui sépare un bon recover d'un mauvais :** descendre dans la
**hiérarchie de vérité** (asm/Frida) au lieu de croire le décompile, et **prouver
en runtime** au lieu de croire la parité.

---

## 5. Pour t'entraîner

Deux cibles toutes prêtes existent déjà : les deux PARTIAL laissés dans `Fire` —
`CGameAnimation::GetHeight` (vtable slot 1) et le type de message à `0x84D328`.
Recover l'un des deux, c'est lever un PARTIAL réel : un exercice complet de bout
en bout, avec un résultat utile à la clé.
