// FROM DELEGATE

// Wrap IPlugSendMsg to filter keyboard events (SKPFUI) so typing in inputs doesn't send
// keys to C++ and to clamp utf8 to a single character to avoid overflows in iPlug's IKeyPress.
(function installIPlugSendMsgGuard(){
  function makeGuarded(sendFn){
    return function(message){
      try {
        if (message && message.msg === 'SKPFUI') {
          const el = document.activeElement;
          const isEditing = !!(el && ((typeof el.matches === 'function' && el.matches('input, textarea, select')) || el.isContentEditable));
          if (isEditing) {
            // Don't forward keyboard events while editing form fields/contentEditable
            return;
          }
          // Clamp utf8 to a single Unicode code point; otherwise send empty string and rely on VK
          if (typeof message.utf8 === 'string') {
            try {
              const cps = Array.from(message.utf8);
              message.utf8 = (cps.length === 1) ? cps[0] : '';
            } catch (_) {
              message.utf8 = (message.utf8.length === 1) ? message.utf8 : '';
            }
          } else {
            message.utf8 = '';
          }
        }
      } catch (e) { /* ignore */ }
      return sendFn(message);
    };
  }

  // If IPlugSendMsg is already present, wrap it; otherwise, retry shortly.
  function tryWrap(attempts){
    if (typeof window.IPlugSendMsg === 'function' && !window.IPlugSendMsg.__guarded) {
      const nativeSend = window.IPlugSendMsg;
      const guarded = makeGuarded(nativeSend);
      guarded.__guarded = true;
      window.IPlugSendMsg = guarded;
    } else if (!window.IPlugSendMsg || !window.IPlugSendMsg.__guarded) {
      if (attempts > 0) setTimeout(() => tryWrap(attempts - 1), 10);
    }
  }

  tryWrap(200); // try for ~2s in case of slow init
})();

function SPVFD(paramIdx, val) {
  console.log("paramIdx: " + paramIdx + " value:" + val);
  OnParamChange(paramIdx, val);
}

function SCVFD(ctrlTag, val) {
  OnControlChange(ctrlTag, val);
//  console.log("SCVFD ctrlTag: " + ctrlTag + " value:" + val);
}

function SCMFD(ctrlTag, msgTag, msg) {
//  var decodedData = window.atob(msg);
  console.log("SCMFD ctrlTag: " + ctrlTag + " msgTag:" + msgTag + "msg:" + msg);
}

function SAMFD(msgTag, dataSize, msg) {
  OnMessage(msgTag, dataSize, msg);
}

function SMMFD(statusByte, dataByte1, dataByte2) {
  console.log("Got MIDI Message" + status + ":" + dataByte1 + ":" + dataByte2);
}

function SSMFD(offset, size, msg) {
  console.log("Got Sysex Message");
}

// FROM UI
// data should be a base64 encoded string
function SAMFUI(msgTag, ctrlTag = -1, data = 0) {
  var message = {
    "msg": "SAMFUI",
    "msgTag": msgTag,
    "ctrlTag": ctrlTag,
    "data": data
  };

  IPlugSendMsg(message);
}

function SMMFUI(statusByte, dataByte1, dataByte2) {
  var message = {
    "msg": "SMMFUI",
    "statusByte": statusByte,
    "dataByte1": dataByte1,
    "dataByte2": dataByte2
  };

  IPlugSendMsg(message);
}

// data should be a base64 encoded string
function SSMFUI(data = 0) {
  var message = {
    "msg": "SSMFUI",
    "data": data
  };

  IPlugSendMsg(message);
}

function EPCFUI(paramIdx) {
  if (paramIdx < 0) {
    console.log("EPCFUI paramIdx must be >= 0")
    return;
  }

  var message = {
    "msg": "EPCFUI",
    "paramIdx": parseInt(paramIdx),
  };

  IPlugSendMsg(message);
}

function BPCFUI(paramIdx) {
  if (paramIdx < 0) {
    console.log("BPCFUI paramIdx must be >= 0")
    return;
  }

  var message = {
    "msg": "BPCFUI",
    "paramIdx": parseInt(paramIdx),
  };

  IPlugSendMsg(message);
}

function SPVFUI(paramIdx, value) {
  if (paramIdx < 0) {
    console.log("SPVFUI paramIdx must be >= 0")
    return;
  }

  var message = {
    "msg": "SPVFUI",
    "paramIdx": parseInt(paramIdx),
    "value": value
  };

  IPlugSendMsg(message);
}

// UI Ready Handshake: notify C++ when the DOM is ready and transport is available
(function installUiReadyHandshake(){
  function trySend(attempts){
    if (typeof window.SAMFUI === 'function' && typeof window.IPlugSendMsg === 'function') {
      try {
        window.SAMFUI(103);
        // After UI is ready, resize window to fit initial content
        // Small delay to ensure UI has fully initialized
        setTimeout(function() {
          if (typeof window.resizeWindowToFit === 'function') {
            window.resizeWindowToFit();
          }
        }, 50);
      } catch (_) {}
      return;
    }
    if (attempts > 0) setTimeout(function(){ trySend(attempts - 1); }, 20);
  }
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function(){ trySend(200); }, { once: true });
  } else {
    trySend(200);
  }
})();
