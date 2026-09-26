// Key-value storage in this browser (IndexedDB): the ROM, saved levels,
// cached captures.

const DB = 'super-alex-kidd-maker', STORE = 'kv';
let opening = null;

function open() {
  if (!opening) {
    opening = new Promise((resolve, reject) => {
      const req = indexedDB.open(DB, 1);
      req.onupgradeneeded = () => req.result.createObjectStore(STORE);
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    });
  }
  return opening;
}

async function run(mode, fn) {
  const db = await open();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, mode);
    const req = fn(tx.objectStore(STORE));
    tx.oncomplete = () => resolve(req && req.result);
    tx.onerror = () => reject(tx.error);
  });
}

export const dbGet = (key) => run('readonly', (s) => s.get(key));
export const dbSet = (key, value) => run('readwrite', (s) => s.put(value, key));
export const dbDelete = (key) => run('readwrite', (s) => s.delete(key));
export const dbKeys = () => run('readonly', (s) => s.getAllKeys());
