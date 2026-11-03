// Installer messages. Adapted from nfsmw-nx-installer (GPL-3.0).
const EN = {
  "platform": "NINTENDO SWITCH · NATIVE PORT",
  "headline": "A small ninja. A big adventure.",
  "lead": "Create a Switch package from your own Xbox 360 copy of Mini Ninjas. Your game files stay on this computer.",
  "privacy": "Local processing. No uploads. No account.",
  "step1": "Choose your game",
  "formatIso": "Disc image (.iso)",
  "formatXex": "Game folder",
  "isoAria": "Xbox 360 disc image",
  "folderAria": "Extracted game folder",
  "progressAria": "Package progress",
  "formatHint": "Select an Xbox 360 ISO, or the extracted folder containing default.xex, scenes and movies.",
  "nothingChosen": "Nothing selected yet.",
  "chosenFile": "{name} ({size})",
  "chosenFolder": "{name}: {count} files ({size})",
  "chosenFolderOne": "{name}: 1 file ({size})",
  "step2": "Create your package",
  "step2First": "First install: includes the port and your game files.",
  "create": "Create mini-ninjas-nx.zip",
  "step2Update": "Already installed? Download only the new program, settings and notices.",
  "createUpdate": "Create update ZIP",
  "step3": "Copy to your Switch",
  "step3Extract": "Extract the ZIP into <code>sdmc:/switch/</code>.",
  "step3Start": "Hold <strong>R</strong> while launching an installed game, then open <code>mini-ninjas.nro</code> from the Homebrew Menu. Album/applet mode is unsupported.",
  "updateHint": "Keep your game files and userdata folder. Back up custom settings before updating.",
  "fullGuide": "Installation guide and controls →",
  "checking": "Checking your game…",
  "supported": "Supported Xbox 360 executable and game data folder found.",
  "executableOnly": "Supported executable. Some game data is missing; only the update package is available.",
  "wrongRelease": "This is Mini Ninjas for Xbox 360, but its executable is not the supported revision. Include this hash when reporting a compatibility issue:",
  "notGame": "This is not a supported Mini Ninjas Xbox 360 executable.",
  "gameName": "Mini Ninjas — Xbox 360",
  "notIso": "Not a readable Xbox 360 ISO",
  "notComplete": "Game files could not be verified",
  "noExecutable": "default.xex was not found. Choose the ISO or the folder containing default.xex, scenes and movies.",
  "incomplete": "Some game data is missing. Select the full extracted game or its disc image.",
  "duplicateFile": "The selected game contains duplicate paths: {path}.",
  "notSupported": "This executable revision is not supported (SHA-256 {hash}).",
  "downloadingBuild": "Downloading Mini Ninjas NX…",
  "downloadFailed": "Could not download {url} (HTTP {status}). Check your connection and try again.",
  "buildMismatch": "The downloaded program does not match the release. Reload this page and try again.",
  "logRelease": "Your copy: Mini Ninjas, {edition}.",
  "logUpdate": "Update package: original game files are not copied.",
  "readingFiles": "Reading your game…",
  "copied": "Copied {path}",
  "done": "Package complete.",
  "noStreaming": "This window cannot stream a large download. Use a regular desktop Chrome, Edge or Firefox window with service workers enabled.",
  "downloadStarted": "Download started ({size}). Keep this page open until it finishes.",
  "finished": "Finished in {seconds} s.",
  "error": "Error: {message}",
  "cancelled": "The download was cancelled.",
  "sourceCode": "Source code",
  "installerSource": "Installer source",
  "releaseLink": "Latest release",
  "legal": "An unofficial fan project. You need your own copy of the Xbox 360 game. Mini Ninjas and its marks belong to their respective owners.",
  "credits": "Installer adapted from <a href=\"https://github.com/infriker/fear-nx\">F.E.A.R. NX</a>, <a href=\"https://github.com/R-drg/project8-sw-installer\">Project8 SW</a> and <a href=\"https://github.com/StevensND/nfsmw-nx-installer\">nfsmw-nx-installer</a>. <a href=\"./NOTICES.md\">GPL-3.0 notices</a>."
};

const RU = {
  platform: 'NINTENDO SWITCH · НАТИВНЫЙ ПОРТ',
  headline: 'Маленький ниндзя. Большое приключение.',
  lead: 'Создайте пакет для Switch из вашей копии Mini Ninjas для Xbox 360. Файлы игры останутся на этом компьютере.',
  privacy: 'Обработка на компьютере. Без загрузки файлов и регистрации.',
  step1: 'Выберите игру', formatIso: 'Образ диска (.iso)', formatXex: 'Папка игры',
  isoAria: 'Образ диска Xbox 360', folderAria: 'Распакованная папка игры', progressAria: 'Создание пакета',
  formatHint: 'Выберите ISO Xbox 360 или распакованную папку с default.xex, scenes и movies.',
  nothingChosen: 'Игра ещё не выбрана.', chosenFile: '{name} ({size})',
  chosenFolder: '{name}: файлов — {count} ({size})', chosenFolderOne: '{name}: 1 файл ({size})',
  step2: 'Создайте пакет', step2First: 'Первая установка включает порт и ваши файлы игры.',
  create: 'Создать mini-ninjas-nx.zip', step2Update: 'Уже установлено? Создайте пакет обновления без файлов игры.',
  createUpdate: 'Создать ZIP обновления', step3: 'Скопируйте на Switch',
  step3Extract: 'Распакуйте ZIP в <code>sdmc:/switch/</code>.',
  step3Start: 'Удерживайте <strong>R</strong> при запуске установленной игры, затем откройте <code>mini-ninjas.nro</code> в Homebrew Menu. Запуск через «Альбом» не поддерживается.',
  updateHint: 'Сохраните файлы игры и сохранения. Перед обновлением сделайте копию изменённых настроек.',
  fullGuide: 'Инструкция по установке и управление →', checking: 'Проверяем игру…',
  supported: 'Найдены поддерживаемая версия Xbox 360 и файлы игры.',
  executableOnly: 'Исполняемый файл поддерживается, но часть файлов игры отсутствует. Доступно только обновление.',
  wrongRelease: 'Это Mini Ninjas для Xbox 360, но версия исполняемого файла не поддерживается. Укажите этот хеш при сообщении о совместимости:',
  notGame: 'Это не поддерживаемый исполняемый файл Mini Ninjas для Xbox 360.',
  gameName: 'Mini Ninjas — Xbox 360', notIso: 'Не удалось прочитать ISO Xbox 360',
  notComplete: 'Не удалось проверить файлы игры',
  noExecutable: 'Не найден default.xex. Выберите ISO или папку с файлами игры.',
  incomplete: 'Отсутствуют необходимые файлы игры. Выберите полный образ или распакованную игру.',
  duplicateFile: 'В выбранной игре повторяется путь: {path}.',
  notSupported: 'Эта версия исполняемого файла не поддерживается (SHA-256 {hash}).',
  downloadingBuild: 'Загружаем Mini Ninjas NX…',
  downloadFailed: 'Не удалось загрузить {url} (HTTP {status}). Проверьте соединение и повторите попытку.',
  buildMismatch: 'Загруженные файлы не соответствуют релизу. Обновите страницу и повторите попытку.',
  logRelease: 'Ваша копия: Mini Ninjas, {edition}.', logUpdate: 'Пакет обновления: файлы игры не копируются.',
  readingFiles: 'Читаем файлы игры…', copied: 'Скопировано: {path}', done: 'Пакет создан.',
  noStreaming: 'В этом окне недоступно потоковое скачивание. Откройте страницу в Chrome, Edge или Firefox на компьютере.',
  downloadStarted: 'Скачивание началось ({size}). Не закрывайте страницу до завершения.',
  finished: 'Готово за {seconds} с.', error: 'Ошибка: {message}', cancelled: 'Скачивание отменено.',
  sourceCode: 'Исходный код', installerSource: 'Код установщика', releaseLink: 'Последний релиз',
  legal: 'Неофициальный фанатский проект. Необходима собственная копия игры для Xbox 360. Права на Mini Ninjas принадлежат их владельцам.',
  credits: 'Установщик создан на основе <a href="https://github.com/infriker/fear-nx">F.E.A.R. NX</a>, <a href="https://github.com/R-drg/project8-sw-installer">Project8 SW</a> и <a href="https://github.com/StevensND/nfsmw-nx-installer">nfsmw-nx-installer</a>. <a href="./NOTICES.md">Лицензии GPL-3.0</a>.'
};
let language = typeof navigator !== 'undefined' && navigator.language?.toLowerCase().startsWith('ru') ? 'ru' : 'en';
export function setLanguage(value) { language = value === 'ru' ? 'ru' : 'en'; }
export function getLanguage() { return language; }

export function t(key, vars = {}) {
  return ((language === 'ru' ? RU[key] : EN[key]) ?? EN[key] ?? key).replace(/\{(\w+)\}/g,
    (match, name) => name in vars ? String(vars[name]) : match);
}

export function formatSize(bytes) {
  const units = ['B', 'KB', 'MB', 'GB'];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit++; }
  return `${value.toFixed(unit ? 1 : 0)} ${units[unit]}`;
}
