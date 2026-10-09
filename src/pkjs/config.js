// Einstellungsseite (Clay) – erscheint in der Pebble-App unter „Einstellungen“.
// Die Seite wird in der eingestellten Sprache erzeugt (nach dem Umstellen
// erscheint sie beim nächsten Öffnen in der neuen Sprache).

// Links im Bereich "Unterstützen". Leer lassen, um den jeweiligen Link auszublenden.
var SUPPORT_URL = 'https://buymeacoffee.com/michaelrunge';
var PROJECT_URL = 'https://github.com/Zeichenfolge/pihole-pebble';

var TEXT = {
  de: {
    intro: 'Pi-hole am Handgelenk: Status, Pause per Knopfdruck und zuletzt blockierte Domains freigeben. ' +
      'Benötigt Pi-hole 6.',
    general: 'Allgemein',
    language: 'Sprache',
    server: 'Pi-hole',
    url: 'Adresse',
    urlHelp: 'Adresse deines Pi-hole, so wie du die Weboberfläche öffnest, z. B. http://192.168.1.2 ' +
      'oder http://pi.hole:8080. „/admin“ darf dabei sein. Eine Adresse im Heimnetz funktioniert nur, ' +
      'wenn das Handy im WLAN zu Hause ist. Tipp: „demo“ eintragen, um die App mit Beispieldaten auszuprobieren.',
    password: 'App-Passwort',
    passwordHelp: 'Empfohlen: ein App-Passwort statt deines normalen Passworts – es funktioniert auch mit ' +
      'Zwei-Faktor-Anmeldung. In Pi-hole: Settings → Web interface / API → oben auf „Expert“ stellen → ' +
      '„Configure app password“ → Passwort kopieren und mit „Enable new app password“ bestätigen. ' +
      'Leer lassen, wenn dein Pi-hole kein Passwort hat.',
    passwordPlaceholder: 'App-Passwort einfügen',
    pause: 'Pause',
    defaultPause: 'Standard-Pause',
    defaultPauseHelp: 'Wird verwendet bei SELECT lang auf dem Startbildschirm und beim Schnellstart.',
    s30: '30 Sekunden', m5: '5 Minuten', m15: '15 Minuten', h1: '1 Stunde',
    quick: 'Schnellstart schaltet sofort um',
    quickHelp: 'Wenn du Pi-hole in den Uhr-Einstellungen auf „Schnellstart“ (Taste lang drücken) legst: ' +
      'aktiv → Standard-Pause, pausiert → wieder aktivieren.',
    vibrate: 'Vibrieren, wenn die Pause endet',
    vibrateHelp: 'Die Uhr meldet sich auch, wenn die App geschlossen ist.',
    list: 'Zuletzt blockiert',
    max: 'Anzahl Einträge',
    save: 'Speichern',
    note: 'Die App spricht nur mit deinem eigenen Pi-hole. Kein Tracking, keine anderen Server.',
    support: 'Unterstützen',
    supportText: 'Pi-hole für Pebble ist kostenlos und Open Source. ' +
      'Wenn dir die App gefällt, freue ich mich über einen Kaffee. ☕',
    supportHint: 'Bitte vorher speichern – der Link öffnet sich in diesem Fenster.',
    project: 'Fehler gefunden oder eine Idee? Auf GitHub kannst du sie melden.',
    disclaimer: 'Kein offizielles Projekt von Pi-hole.'
  },
  en: {
    intro: 'Pi-hole on your wrist: status, pause with one button and allow recently blocked domains. ' +
      'Requires Pi-hole 6.',
    general: 'General',
    language: 'Language',
    server: 'Pi-hole',
    url: 'Address',
    urlHelp: 'The address of your Pi-hole as you open its web interface, e.g. http://192.168.1.2 ' +
      'or http://pi.hole:8080. "/admin" may be included. A home network address only works while the phone ' +
      'is on your home Wi-Fi. Tip: enter "demo" to try the app with sample data.',
    password: 'App password',
    passwordHelp: 'Recommended: an app password instead of your normal password – it also works with ' +
      'two-factor authentication. In Pi-hole: Settings → Web interface / API → switch to "Expert" at the top → ' +
      '"Configure app password" → copy the password and confirm with "Enable new app password". ' +
      'Leave empty if your Pi-hole has no password.',
    passwordPlaceholder: 'Paste app password',
    pause: 'Pause',
    defaultPause: 'Default pause',
    defaultPauseHelp: 'Used when holding SELECT on the main screen and for Quick Launch.',
    s30: '30 seconds', m5: '5 minutes', m15: '15 minutes', h1: '1 hour',
    quick: 'Quick Launch toggles immediately',
    quickHelp: 'If you put Pi-hole on a Quick Launch button in the watch settings (long press): ' +
      'active → default pause, paused → resume.',
    vibrate: 'Vibrate when the pause ends',
    vibrateHelp: 'The watch alerts you even when the app is closed.',
    list: 'Recently blocked',
    max: 'Number of entries',
    save: 'Save',
    note: 'The app only talks to your own Pi-hole. No tracking, no other servers.',
    support: 'Support',
    supportText: 'Pi-hole for Pebble is free and open source. ' +
      'If you enjoy it, I\'d be happy about a coffee. ☕',
    supportHint: 'Please save first – the link opens in this window.',
    project: 'Found a bug or have an idea? Report it on GitHub.',
    disclaimer: 'Not an official Pi-hole project.'
  }
};

function button(url, label) {
  return '<a href="' + url + '" style="display:block;margin:12px 0 4px;padding:12px;border-radius:8px;' +
    'background:#ffdd00;color:#000;font-weight:bold;text-align:center;text-decoration:none;">' + label + '</a>';
}

// Bereich "Unterstützen" – steht unter dem Speichern-Knopf, damit niemand
// ungespeicherte Einstellungen verliert, wenn er den Link öffnet.
function supportSection(t) {
  var items = [{ type: 'heading', defaultValue: t.support }];
  if (SUPPORT_URL) {
    items.push({ type: 'text', defaultValue: t.supportText + button(SUPPORT_URL, '☕ Buy me a coffee') });
    items.push({ type: 'text', defaultValue: '<small>' + t.supportHint + '</small>' });
  }
  if (PROJECT_URL) {
    items.push({ type: 'text', defaultValue: t.project + ' <a href="' + PROJECT_URL + '">GitHub</a>' });
  }
  items.push({ type: 'text', defaultValue: '<small>' + t.disclaimer + '</small>' });
  return { type: 'section', items: items };
}

module.exports = function (lang) {
  var t = TEXT[lang] || TEXT.de;
  return [
    { type: 'heading', defaultValue: 'Pi-hole' },
    { type: 'text', defaultValue: t.intro },
    {
      type: 'section',
      items: [
        { type: 'heading', defaultValue: t.general },
        {
          type: 'select', messageKey: 'CfgLang', label: t.language, defaultValue: 'de',
          options: [{ label: 'Deutsch', value: 'de' }, { label: 'English', value: 'en' }]
        }
      ]
    },
    {
      type: 'section',
      items: [
        { type: 'heading', defaultValue: t.server },
        {
          type: 'input', messageKey: 'CfgUrl', label: t.url, defaultValue: '',
          description: t.urlHelp,
          attributes: { placeholder: 'http://192.168.1.2', type: 'text', autocapitalize: 'off', autocorrect: 'off', spellcheck: 'false' }
        },
        {
          type: 'input', messageKey: 'CfgPassword', label: t.password, defaultValue: '',
          description: t.passwordHelp,
          attributes: { placeholder: t.passwordPlaceholder, type: 'password', autocapitalize: 'off', autocorrect: 'off' }
        }
      ]
    },
    {
      type: 'section',
      items: [
        { type: 'heading', defaultValue: t.pause },
        {
          type: 'select', messageKey: 'CfgPause', label: t.defaultPause, defaultValue: '300',
          description: t.defaultPauseHelp,
          options: [
            { label: t.s30, value: '30' }, { label: t.m5, value: '300' },
            { label: t.m15, value: '900' }, { label: t.h1, value: '3600' }
          ]
        },
        { type: 'toggle', messageKey: 'CfgQuick', label: t.quick, description: t.quickHelp, defaultValue: true },
        { type: 'toggle', messageKey: 'CfgVibrate', label: t.vibrate, description: t.vibrateHelp, defaultValue: true }
      ]
    },
    {
      type: 'section',
      items: [
        { type: 'heading', defaultValue: t.list },
        { type: 'slider', messageKey: 'CfgMax', label: t.max, defaultValue: 20, min: 5, max: 30, step: 5 }
      ]
    },
    { type: 'text', defaultValue: t.note },
    { type: 'submit', defaultValue: t.save },
    supportSection(t)
  ];
};
