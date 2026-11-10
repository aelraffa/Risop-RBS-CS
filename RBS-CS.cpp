#include <chrono>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX 105

char inputName[100];
FILE *outFile = NULL;

// data input (setelah diseimbangkan)
int row, col, matrix[MAX][MAX], s[MAX], d[MAX];
int dummyRow = 0, dummyCol = 0;

// data kerja metode
int TC[MAX], supply[MAX][MAX], sumRow[MAX],
    status[MAX]; // supply[i][j] = alokasi sel (i,j)
int verbose = 1;

// status baris
#define SATISFIED 1
#define NEED_SUPPLY 2
#define EXCESS_ROW 3

// satu langkah pemindahan
typedef struct {
  int chain;
  int i, j; // sel donor (baris ER i, kolom j)
  int k;
  int l, r;
  int delta;  // kenaikan biaya per unit
  int amount; // unit yang dipindah
  int priority;
} Move;

void out(const char *fmt, ...) {
  if (!verbose)
    return;
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  if (outFile) {
    va_start(ap, fmt);
    vfprintf(outFile, fmt, ap);
    va_end(ap);
  }
}
void outAlways(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  if (outFile) {
    va_start(ap, fmt);
    vfprintf(outFile, fmt, ap);
    va_end(ap);
  }
}

int readInput() {
  FILE *inFile = fopen(inputName, "r");
  if (inFile == NULL) {
    printf("Error! Could not open file\n");
    return 0;
  }
  int i, j, totS = 0, totD = 0;
  fscanf(inFile, "%d %d", &row, &col);
  for (i = 0; i < row; i++)
    for (j = 0; j < col; j++)
      fscanf(inFile, "%d", &matrix[i][j]);
  for (i = 0; i < row; i++) {
    fscanf(inFile, "%d", &s[i]);
    totS += s[i];
  }
  for (j = 0; j < col; j++) {
    fscanf(inFile, "%d", &d[j]);
    totD += d[j];
  }
  fclose(inFile);

  // Langkah 1: buat TP seimbang dengan baris/kolom dummy (cost 0)
  dummyRow = dummyCol = 0;
  if (totS > totD) {
    for (i = 0; i < row; i++)
      matrix[i][col] = 0;
    d[col++] = totS - totD;
    dummyCol = 1;
  } else if (totD > totS) {
    for (j = 0; j < col; j++)
      matrix[row][j] = 0;
    s[row++] = totD - totS;
    dummyRow = 1;
  }
  return 1;
}

void printTable() {
  int i, j;
  char stat[][12] = {"SATISFIED", "NEED SUPPLY", "EXCESS ROW"};
  out("\n        ");
  for (j = 0; j < col; j++)
    out("D%-9d", j + 1);
  out("| Supply   TC    TA    Status\n");
  for (i = 0; i < row; i++) {
    out("  S%-4d ", i + 1);
    for (j = 0; j < col; j++) {
      if (supply[i][j])
        out("%3d [%3d] ", matrix[i][j], supply[i][j]);
      else
        out("%3d       ", matrix[i][j]);
    }
    out("| %-8d %-5d %-5d %s\n", s[i], TC[i], sumRow[i],
        status[i] ? stat[status[i] - 1] : "-");
  }
  out("  Demand ");
  for (j = 0; j < col; j++)
    out("%-10d", d[j]);
  out("\n\n");
}

// Langkah 2: TC, FLC, alokasi awal
void minimumCell() {
  int i, j, minimum;
  for (i = 0; i < row; i++) {
    TC[i] = 0;
    sumRow[i] = 0;
    status[i] = 0;
    for (j = 0; j < col; j++) {
      TC[i] += matrix[i][j];
      supply[i][j] = 0;
    }
  }
  out("Langkah 2: FLC tiap kolom (seri -> TC terbesar)\n");
  for (j = 0; j < col; j++) {
    minimum = 0;
    int same = 1;
    for (i = 1; i < row; i++) {
      if (matrix[minimum][j] > matrix[i][j]) {
        minimum = i;
        same = 1;
      } else if (matrix[minimum][j] == matrix[i][j]) {
        same++;
        if (TC[i] > TC[minimum])
          minimum = i;
      }
    }
    supply[minimum][j] = d[j];
    out("  D%d: FLC = S%d (cost %d)%s, alokasi %d\n", j + 1, minimum + 1,
        matrix[minimum][j], same > 1 ? " [seri, dipilih TC terbesar]" : "",
        d[j]);
  }
}

// Langkah 3: TA dan status

int updateStatus() {
  int i, j, nExcess = 0;
  for (i = 0; i < row; i++) {
    sumRow[i] = 0;
    for (j = 0; j < col; j++)
      sumRow[i] += supply[i][j];
    if (sumRow[i] == s[i])
      status[i] = SATISFIED;
    else if (sumRow[i] < s[i])
      status[i] = NEED_SUPPLY;
    else {
      status[i] = EXCESS_ROW;
      nExcess++;
    }
  }
  return nExcess;
}

// baris yang di-satisfy: C x S FLC vs C x S SLC | jika sama -> indeks baris
// terkecil (RBSM)
int priorityRow(int don, int rec, float ratioFLC, float ratioSLC) {
  if (ratioFLC > ratioSLC)
    return don;
  if (ratioFLC < ratioSLC)
    return rec;
  return don < rec ? don : rec;
}

// jumlah unit donasi: |supply - TA| baris prioritas, dibatasi alokasi sel donor
// (RBSM)
int donAmount(int don, int rec, int j, float ratioFLC, float ratioSLC,
              int *priority) {
  *priority = priorityRow(don, rec, ratioFLC, ratioSLC);
  int maxDon = s[*priority] - sumRow[*priority];
  if (maxDon < 0)
    maxDon = -maxDon;
  if (supply[don][j] < maxDon)
    maxDon = supply[don][j];
  return maxDon;
}

/*Langkah 4-5: Diff, SDiff ----------
  tiap kolom: pasangan (sel ER, baris NS) dengan |cost ER - cost NS| terkecil
 (seri -> TC baris NS terbesar)
 - SDiff = Diff terkecil antar kolom; jika beberapa kolom seri -> maxDon x cost
 FLC terkecil (seri lagi -> kolom indeks terkecil)
 - rasio C x S yang dipakai untuk prioritas diambil dari kolom SDiff pertama */
int findDirect(Move *best) {
  int i, j, k, smallestIndex = 0, flag = 0;
  int localSmallest[MAX], localDon[MAX], localRec[MAX];
  out("Langkah 4: Diff terkecil tiap kolom (sel ER -> baris NS, |selisih "
      "cost|)\n");
  for (j = 0; j < col; j++) {
    localSmallest[j] = INT_MAX;
    localDon[j] = localRec[j] = -1;
    for (i = 0; i < row; i++) {
      if (status[i] != EXCESS_ROW || !supply[i][j])
        continue;
      for (k = 0; k < row; k++) {
        if (status[k] != NEED_SUPPLY)
          continue;
        int temp = matrix[i][j] - matrix[k][j];
        if (temp < 0)
          temp = -temp;
        if (localSmallest[j] > temp) {
          localSmallest[j] = temp;
          localDon[j] = i;
          localRec[j] = k;
        } else if (localSmallest[j] == temp && TC[k] > TC[localRec[j]]) {
          localDon[j] = i;
          localRec[j] = k;
        }
      }
    }
    if (localRec[j] >= 0)
      out("  D%d: sel (S%d,D%d) cost %d -> S%d cost %d, Diff = %d\n", j + 1,
          localDon[j] + 1, j + 1, matrix[localDon[j]][j], localRec[j] + 1,
          matrix[localRec[j]][j], localSmallest[j]);
    if (localSmallest[smallestIndex] > localSmallest[j] || smallestIndex == j) {
      smallestIndex = j;
      flag = 0;
    } else if (localSmallest[smallestIndex] == localSmallest[j])
      flag = 1;
  }
  if (localRec[smallestIndex] < 0)
    return 0;

  int d0 = localDon[smallestIndex], r0 = localRec[smallestIndex];
  float ratioFLC = (float)s[d0] * matrix[d0][smallestIndex];
  float ratioSLC = (float)s[r0] * matrix[r0][smallestIndex];
  if (flag) {
    out("Langkah 5: SDiff seri di beberapa kolom, pilih maxDon x cost FLC "
        "terkecil\n");
    int p;
    for (j = smallestIndex + 1; j < col; j++) {
      if (localSmallest[smallestIndex] != localSmallest[j])
        continue;
      long hk1 =
          (long)donAmount(localDon[smallestIndex], localRec[smallestIndex],
                          smallestIndex, ratioFLC, ratioSLC, &p) *
          matrix[localDon[smallestIndex]][smallestIndex];
      long hk2 =
          (long)donAmount(localDon[j], localRec[j], j, ratioFLC, ratioSLC, &p) *
          matrix[localDon[j]][j];
      out("  D%d: %ld vs D%d: %ld\n", smallestIndex + 1, hk1, j + 1, hk2);
      if (hk1 > hk2)
        smallestIndex = j;
    }
  }
  memset(best, 0, sizeof *best);
  best->chain = 0;
  best->i = localDon[smallestIndex];
  best->j = smallestIndex;
  best->k = localRec[smallestIndex];
  best->delta = matrix[best->k][best->j] -
                matrix[best->i][best->j]; // kenaikan biaya per unit (bertanda)
  best->amount =
      donAmount(best->i, best->k, best->j, ratioFLC, ratioSLC, &best->priority);
  out("Langkah 5: SDiff = %d di sel (S%d,D%d) -> (S%d,D%d)\n",
      localSmallest[smallestIndex], best->i + 1, best->j + 1, best->k + 1,
      best->j + 1);
  return 1;
}

/* ==================== [MODIFIKASI CT-RBSM] MULAI ====================
 * Langkah 6 (BARU): transfer berantai ER -> S -> NS (Teroptimasi O(mn + n^2))
 * Fungsi ini tidak ada di RBSM asli. RBSM asli hanya memindahkan langsung
 * ER -> NS (findDirect); di sini baris S boleh menjadi perantara.
 *
 * Optimasi Dekomposisi Independen:
 *   1. Precompute donor ER terbaik tiap kolom j (cost terbesar, seri -> cap
 * terbesar): O(mn)
 *   2. Precompute penerima NS terbaik tiap kolom l (cost terkecil, seri -> need
 * terbesar): O(mn)
 *   3. Pasangkan lewat baris perantara S (k) pada sel teralokasi: O(n(m + n))
 * ---------------------------------------------------------------------- */
int findChain(Move *best) {
  int idxJ, idxK, i, j, k, l, r, found = 0, printed = 0;
  int satRows[MAX], nSat = 0;
  int nsRows[MAX], nNS = 0;
  int donCols[MAX], nDon = 0;
  int donRow[MAX], donCost[MAX], donCap[MAX];
  int recRow[MAX], recCost[MAX], recCap[MAX];

  out("Langkah 6 (baru): kandidat transfer berantai ER -> S -> NS\n");

  // Kelompokkan baris berdasarkan status - O(m)
  for (i = 0; i < row; i++) {
    if (status[i] == SATISFIED)
      satRows[nSat++] = i;
    else if (status[i] == NEED_SUPPLY)
      nsRows[nNS++] = i;
  }
  if (nSat == 0 || nNS == 0) {
    out("  Tidak ada kandidat (belum ada baris Satisfied yang bisa jadi "
        "perantara)\n");
    return 0;
  }

  for (j = 0; j < col; j++) {
    donRow[j] = -1;
    donCost[j] = -1;
    donCap[j] = 0;
    recRow[j] = -2; // -2 = belum dihitung (lazy evaluation per kolom l)
  }

  // Tahap 1: Precompute donor ER terbaik hanya pada baris ER - O(|ER| * n)
  for (i = 0; i < row; i++) {
    if (status[i] != EXCESS_ROW)
      continue;
    int ex = sumRow[i] - s[i];
    for (j = 0; j < col; j++) {
      if (!supply[i][j])
        continue;
      int cap = supply[i][j] < ex ? supply[i][j] : ex;
      if (donRow[j] < 0)
        donCols[nDon++] = j;
      if (donRow[j] < 0 || matrix[i][j] > donCost[j] ||
          (matrix[i][j] == donCost[j] && cap > donCap[j])) {
        donRow[j] = i;
        donCost[j] = matrix[i][j];
        donCap[j] = cap;
      }
    }
  }

  // Tahap 2: Evaluasi jalur ER -> S -> NS hanya pada kolom donor & baris S
  // aktif
  for (idxJ = 0; idxJ < nDon; idxJ++) {
    j = donCols[idxJ];
    i = donRow[j];
    for (idxK = 0; idxK < nSat; idxK++) {
      k = satRows[idxK];
      int d1 = matrix[k][j] - donCost[j];
      for (l = 0; l < col; l++) {
        if (l == j || !supply[k][l])
          continue; // perantara melepas sel teralokasi di kolom lain
        if (recRow[l] == -2) {
          // Hitung penerima NS terbaik di kolom l hanya jika kolom l terpakai
          int bestR = -1, bestC = INT_MAX, bestNeed = 0, idxR;
          for (idxR = 0; idxR < nNS; idxR++) {
            int rr = nsRows[idxR];
            int need = s[rr] - sumRow[rr];
            if (matrix[rr][l] < bestC ||
                (matrix[rr][l] == bestC && need > bestNeed)) {
              bestR = rr;
              bestC = matrix[rr][l];
              bestNeed = need;
            }
          }
          recRow[l] = bestR;
          recCost[l] = bestC;
          recCap[l] = bestNeed;
        }
        r = recRow[l];
        if (r < 0)
          continue;
        int d2 = recCost[l] - matrix[k][l];
        int delta = d1 + d2;
        int amt = donCap[j];
        if (supply[k][l] < amt)
          amt = supply[k][l];
        if (recCap[l] < amt)
          amt = recCap[l];
        if (printed < 30) {
          out("  S%d->S%d di D%d (%+d), S%d->S%d di D%d (%+d): delta = "
              "%+d, theta = %d\n",
              i + 1, k + 1, j + 1, d1, k + 1, r + 1, l + 1, d2, delta, amt);
          printed++;
        }
        // delta terkecil; jika sama, theta terbesar
        if (!found || delta < best->delta ||
            (delta == best->delta && amt > best->amount)) {
          memset(best, 0, sizeof *best);
          best->chain = 1;
          best->i = i;
          best->j = j;
          best->k = k;
          best->l = l;
          best->r = r;
          best->delta = delta;
          best->amount = amt;
          found = 1;
        }
      }
    }
  }
  if (!found)
    out("  Tidak ada kandidat (belum ada baris Satisfied yang bisa jadi "
        "perantara)\n");
  return found;
}
/* ==================== [MODIFIKASI CT-RBSM] SELESAI ==================== */

/* ---------- pindahkan unit ---------- */
void applyMove(Move *m) {
  if (!m->chain) {
    // transfer langsung FLC -> SLC di kolom yang sama (RBSM)
    supply[m->i][m->j] -= m->amount;
    supply[m->k][m->j] += m->amount;
  } else {
    // transfer melalui 4 sel (RBS-CS)
    supply[m->i][m->j] -= m->amount; // ER melepas di kolom j
    supply[m->k][m->j] += m->amount; // perantara menerima di kolom j
    supply[m->k][m->l] -= m->amount; // perantara melepas di kolom l
    supply[m->r][m->l] += m->amount; // NS menerima di kolom l
  }
}

long countTCC() {
  long TCC = 0;
  int i, j;
  for (i = 0; i < row; i++)
    for (j = 0; j < col; j++)
      TCC += (long)matrix[i][j] * supply[i][j];
  return TCC;
}

int feasible() {
  int i, j;
  for (i = 0; i < row; i++) {
    int t = 0;
    for (j = 0; j < col; j++) {
      if (supply[i][j] < 0)
        return 0;
      t += supply[i][j];
    }
    if (t != s[i])
      return 0;
  }
  for (j = 0; j < col; j++) {
    int t = 0;
    for (i = 0; i < row; i++)
      t += supply[i][j];
    if (t != d[j])
      return 0;
  }
  return 1;
}

/* ---------- jalankan satu metode; useChain = 0 RBSM asli, 1 CT-RBSM ----------
 * Alur loop = RBSM asli; yang berbeda hanya blok [MODIFIKASI CT-RBSM] di
 * dalamnya. */
long solve(int useChain, int *iterations) {
  minimumCell();
  updateStatus();
  out("Langkah 3: TA dan status awal\n");
  printTable();

  int it = 0;
  while (updateStatus() > 0 && it < 1000) {
    it++;
    out("=== Iterasi ke-%d ===\n", it);
    Move direct, chain, chosen;
    if (!findDirect(&direct))
      break; // [RBSM ASLI] langkah 4-5
    chosen = direct;

    // ===== [MODIFIKASI CT-RBSM] MULAI: bandingkan delta berantai dengan SDiff
    // =====
    if (useChain && findChain(&chain)) {
      out("  Delta berantai terkecil = %+d, SDiff = %+d -> ", chain.delta,
          direct.delta);
      if (chain.delta < direct.delta) {
        chosen = chain;
        out("pakai TRANSFER BERANTAI\n");
      } else
        out("tetap pakai transfer langsung (aturan RBSM)\n");
    }
    // ===== [MODIFIKASI CT-RBSM] SELESAI =====

    if (!chosen.chain) {
      // [RBSM ASLI] aturan satisfy (langkah 6 RBSM / langkah 7 CT-RBSM)
      int i = chosen.i, j = chosen.j, k = chosen.k;
      out("Langkah %d: supply FLC = %d, supply SLC = %d; C x S FLC = %d x %d = "
          "%ld, C x S SLC = %d x %d = %ld\n",
          useChain ? 7 : 6, s[i], s[k], matrix[i][j], s[i],
          (long)matrix[i][j] * s[i], matrix[k][j], s[k],
          (long)matrix[k][j] * s[k]);
      out("  -> satisfy %s (S%d), pindahkan %d unit dari (S%d,D%d) ke "
          "(S%d,D%d), biaya %+d\n",
          chosen.priority == i ? "FLC" : "SLC", chosen.priority + 1,
          chosen.amount, i + 1, j + 1, k + 1, j + 1,
          chosen.amount * chosen.delta);
    } else {
      // [MODIFIKASI CT-RBSM] cetak transfer berantai
      out("  -> theta = %d unit: (S%d,D%d) -%d, (S%d,D%d) +%d, (S%d,D%d) -%d, "
          "(S%d,D%d) +%d, biaya %+d\n",
          chosen.amount, chosen.i + 1, chosen.j + 1, chosen.amount,
          chosen.k + 1, chosen.j + 1, chosen.amount, chosen.k + 1, chosen.l + 1,
          chosen.amount, chosen.r + 1, chosen.l + 1, chosen.amount,
          chosen.amount * chosen.delta);
    }
    if (chosen.amount <= 0) {
      out("  Tidak ada unit yang bisa dipindah, berhenti.\n");
      break;
    }
    applyMove(&chosen);
    updateStatus();
    printTable();
  }
  *iterations = it;
  return countTCC();
}

/* ---------- [TAMBAHAN PROGRAM] ukur runtime rata-rata (tanpa output)
 * ---------- */
long lastReps = 0;
double runtimeMicro(int useChain) {
  int it;
  int saved = verbose;
  verbose = 0;
  // ulangi sampai total waktu >= 0.2 detik supaya resolusi timer tidak
  // berpengaruh
  long reps = 1000;
  double total;
  while (1) {
    auto t0 = std::chrono::steady_clock::now();
    for (long r = 0; r < reps; r++)
      solve(useChain, &it);
    auto t1 = std::chrono::steady_clock::now();
    total = std::chrono::duration<double, std::micro>(t1 - t0).count();
    if (total >= 200000.0 || reps >= 100000000L)
      break;
    reps *= 4;
  }
  verbose = saved;
  lastReps = reps;
  return total / reps;
}

/* Jalankan RBS-CS dan tampilkan proses serta ringkasan hasilnya. */
void compute() {
  if (!readInput())
    return;
  outFile = fopen("output.txt", "w");
  if (dummyRow)
    outAlways("TP tidak seimbang: ditambah baris dummy S%d (cost 0)\n", row);
  if (dummyCol)
    outAlways("TP tidak seimbang: ditambah kolom dummy D%d (cost 0)\n", col);

  int iterations;
  verbose = 1;
  outAlways("\n################ RBS-CS ################\n");
  long ttc = solve(1, &iterations);
  int isFeasible = feasible();
  outAlways("TTC RBS-CS = %ld (%d iterasi)%s\n", ttc, iterations,
            isFeasible ? "" : "  [TIDAK FEASIBLE]");

  double runtime = runtimeMicro(1);
  outAlways("\n================ HASIL RBS-CS ================\n");
  outAlways("%-26s %10s %10s %14s\n", "Metode", "TTC", "Iterasi",
            "Runtime (us)");
  outAlways("%-26s %10ld %10d %14.3f\n", "RBS-CS", ttc, iterations, runtime);
  outAlways("(Runtime = rata-rata per eksekusi, diukur dari >= 0.2 detik "
            "pengulangan tanpa output)\n\n");

  if (outFile) {
    fclose(outFile);
    outFile = NULL;
  }
  printf("Output juga ditulis ke output.txt\n");
}

void custom() {
  printf("Name = ");
  scanf("%99s", inputName);
}

int main() {
  int option = 0;
  printf("What is your input-file's name?\n1 = Default (input.txt)\n2 = "
         "Custom\n3 = Exit\n");
  printf("Option = ");
  if (scanf("%d", &option) != 1)
    return 0;
  while (option != 3) {
    if (option == 1) {
      strcpy(inputName, "input.txt");
      compute();
    } else if (option == 2) {
      custom();
      compute();
    } else
      printf("Not Valid\n");
    printf("Option = ");
    if (scanf("%d", &option) != 1)
      break;
  }
  printf("Program Stop\n\n");
  return 0;
}
