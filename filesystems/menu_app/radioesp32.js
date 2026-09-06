var version = "";
var id=null;

var partsize;
var partsize0;
var partsize1;
var targetedVersion = "0.0.0";

var websock = null;
var heartbeat_msg = '--heartbeat--', heartbeat_interval = null, missed_heartbeats = 0;
var wsUri = "ws://" + window.location.host + "/ws";
//var data = [];
var ajaxobj;
var config = {};
var backupstarted = false;
var restorestarted = false;
var gotInitialData = false;
var wsConnectionPresent = false;
var running = null;
var radioesp32;
var urls = ["","",""];


function bsModal(message, title, isConfirm) {
  title = title || 'Warning';
  isConfirm = isConfirm || false;

  return new Promise(function(resolve) {
    var $modal = $('#customModal');
    var confirmed = false;
    $modal.find('#customModalLabel').text(title);
    $modal.find('#customModalBody').text(message);
    if (isConfirm) {
      $modal.find('#customModalCancelBtn').show();
    } else {
      $modal.find('#customModalCancelBtn').hide();
    }
    $modal.find('#customModalConfirmBtn').off('click').on('click', function() {
      confirmed = true;
      $modal.modal('hide');
    });
    $modal.off('hidden.bs.modal').on('hidden.bs.modal', function() {
      resolve(confirmed);
    });
    $modal.modal('show');
  });
};

async function alert_(msg)
{
  await bsModal(msg);
}

async function backupFailed(msg)
{
  await bsModal(msg, 'Backup failed');
}

async function updateFailed(msg) {
  await bsModal(msg, 'Update failed');
}

async function tooLarge(filemb, limitmb) {
  await bsModal("File is too large!\n\nSelected file: " + filemb + " MB\nTarget partition limit: " + limitmb + " MB\n\nFlash operation aborted for device safety.", 'Error');
}
async function failedToReadFw(msg) {
  await bsModal("Failed to read firmware file from GitHub: " + msg, 'Failure');
}

async function restoreConfig(jsn)
{
  var confirm_ = await bsModal("File seems to be valid, do you wish to continue?", 'Restore Settings', true);
  if (confirm_)
  {
		config = jsn;
        if (!config.hasOwnProperty("bluetooth")) //upgrade from version 1.X.X !
		{
			config.bluetooth = {"btname":"RadioESP32","btauto":1,"btcount":32,"btaction":1};
		}
		uncommited();
  }
}

async function updateRadio(ver, binfile) {
  var confirm_ = await bsModal("Do you really want to update Internet Radio to version v" + ver + "?", 'Update Radio Firmware', true);
  if (confirm_)
  {
    startOtaUpdate(binfile, "RADIO");
  }
}

async function updateBluetooth(ver, binfile) {
  var confirm_ = await bsModal("Do you really want to update Bluetooth Speaker to version v" + ver + "?", 'Update Bluetooth Firmware', true);
  if (confirm_)
  {
    startOtaUpdate(binfile, "BTLS");
  }
}

async function updateLocal(filename, target, fl)
{
  var confirm_ = await bsModal("Do you really want to flash " + filename + " into " + target + "?");
    if (confirm_)
    {
        if (heartbeat_interval !== null) {
            clearInterval(heartbeat_interval);
            heartbeat_interval = null;
            missed_heartbeats = 0;
        }

$("#flash-progress-container").show();
$("#flash-progress-bar").css("width", "0%").removeClass('progress-bar-success').addClass('progress-bar-danger');
$("#flash-progress-text").text("Reading local file from disk...");

        var reader = new FileReader();
        reader.onload = function(evt) {
            var arrayBuffer = evt.target.result;
            console.log("Local file loaded to browser. Size: " + arrayBuffer.byteLength + " bytes.");
            
            otaBuffer = arrayBuffer;
            currentOffset = 0;
            var originalOnMessage = websock.onmessage;
            websock.onmessage = function(event) {
                var reply = event.data;
                if (reply === "STATUS: READY_FOR_DATA" || reply === "STATUS: CHUNK_OK") {
                    setTimeout(function() { sendNextOtaChunk(); }, 30);
                }
            else if (reply === "STATUS: SUCCESS") {
                $("#flash-progress-container").fadeOut(300, function() {
                    
                    $("#reboot-countdown-container").fadeIn(300);
                    
                    var timeLeft = 5;
                    $("#countdown-number").text(timeLeft);

                    var countdownTimer = setInterval(function() {
                        timeLeft--;
                        $("#countdown-number").text(timeLeft);
                        $("#reboot-spinner").css("transform", "rotate(" + ((5 - timeLeft) * 72) + "deg)");

                        if (timeLeft <= 0) {
                            clearInterval(countdownTimer);
                            window.location.reload();
                        }
                    }, 1000);
                });
                otaBuffer = null;
                currentOffset = 0;
                websock.onmessage = originalOnMessage;
            }

                else if (reply.startsWith("ERROR:")) {
                    $("#flash-progress-container").hide();
                    updateFailed(reply.substring(7));
                    otaBuffer = null;
                    currentOffset = 0;
                    websock.onmessage = originalOnMessage;
                    restartHeartbeat();
                }
            };
            websock.send(JSON.stringify({ "command": "ota_start", "target": target, "size": arrayBuffer.byteLength }));
        };
        
        reader.readAsArrayBuffer(fl);
    }
}


    function downloadBackupWithProgress(endpointUrl, fileName) {
        $("#backup-progress-container").show();
        $("#backup-progress-text").text("Starting download...");
        $("#backup-progress-bar").css("width", "0%");

        var cleanUrl = window.location.protocol + "//" + window.location.host + endpointUrl;

        var USERNAME = "admin";
        var PASSWORD = "kwUghLrp6hKqO72g";

        var headers = new Headers();
        headers.append("Authorization", "Basic " + btoa(USERNAME + ":" + PASSWORD));

        fetch(cleanUrl, { method: 'GET', headers: headers })
        .then(response => {
            if (!response.ok) throw new Error("HTTP error " + response.status);
            
            const totalBytes = parseInt(response.headers.get('content-length'), 10);
            const reader = response.body.getReader();
            let loadedBytes = 0;
            let chunks = [];

            function read() {
                return reader.read().then(({ done, value }) => {
                    if (done) {
                        var blob = new Blob(chunks, { type: 'application/octet-stream' });
                        
                        var link = document.createElement('a');
                        link.href = window.URL.createObjectURL(blob);
                        link.download = fileName;
                        document.body.appendChild(link);
                        link.click();
                        document.body.removeChild(link);

                        $("#backup-progress-text").text("Download complete!");
                        setTimeout(function() {
                            $("#backup-progress-container").fadeOut();
                        }, 2000);
                        return;
                    }
                    
                    loadedBytes += value.byteLength;
                    chunks.push(value);

                    if (totalBytes) {
                        var progress = Math.round((loadedBytes / totalBytes) * 100);
                        $("#backup-progress-text").text("Downloading backup: " + progress + "% (" + Math.round(loadedBytes/1024) + " KB)");
                        $("#backup-progress-bar").css("width", progress + "%");
                    }
                    return read();
                });
            }
            return read();
        })
        .catch(error => {
            $("#backup-progress-container").hide();
            backupFailed(error.message);
        });
    }


function getVersion(filename) {
    // Find X.X.X version
    const match = filename.match(/v?(\d+\.\d+\.\d+)/i);
    return match ? match[1] : "0.0.0";
}

function listupdmanager(obj)
{
    $("#radio-current").html(obj.binaries.radio.version);
    $("#bt-current").html(obj.binaries.btls.version);
    $("#upman-current").html(obj.binaries.upman.version);

var repoUrl = "/Pako2/RadioESP32"; 
// Base URL for download latest files from GitHub: 
var downloadBaseUrl = "https://github.com" + repoUrl + "/releases/latest/download/"; // for live operation 
var jsonUrl = "https://raw.githubusercontent.com"+repoUrl+"/refs/heads/main/bin/latest.json";

    // GLOBAL VARIABLES WITHIN THIS FUNCTION
    var radioBinFile = "";
    var btBinFile = "";
    var umBinFile = "";
    var gitRadio = "0.0.0";
    var gitBt = "0.0.0";
    var gitUm = "0.0.0";

    // 2. Download info-file latest.json
    $.getJSON(jsonUrl, function(data)
    {
        radioBinFile = data.radio_file;
        btBinFile = data.btls_file; 
        umBinFile = data.upman_file; 
        urls[0] = downloadBaseUrl + radioBinFile;
        urls[1] = downloadBaseUrl + btBinFile;
        urls[2] = downloadBaseUrl + umBinFile;

        // --- Radio processing ---
        var currentRadio = $("#radio-current").text().replace('v', '').trim();
        gitRadio = data.radio_version.replace('v', '').trim();
        targetedVersion = gitRadio;

        $("#radio-github").html('<span class="label label-info">v' + gitRadio + '</span>');
        
        if (isNewerVersion(currentRadio, gitRadio))
        {
            $("#btn-update-radio").prop("disabled", false);
            $("#btn-download-radio").prop("disabled", false);
            $("#radio-github").find('.label').removeClass('label-info').addClass('label-success');
        } else
        {
            $("#radio-github").find('.label').removeClass('label-info').addClass('label-default');
        }

        // --- Bluetooth processing ---
        var currentBt = $("#bt-current").text().replace('v', '').trim();
        gitBt = data.btls_version.replace('v', '').trim();
        targetedVesion = gitBt;

        $("#bt-github").html('<span class="label label-info">v' + gitBt + '</span>');

        if (isNewerVersion(currentBt, gitBt)) {
            $("#btn-update-bt").prop("disabled", false);
            $("#btn-download-bt").prop("disabled", false);
            $("#bt-github").find('.label').removeClass('label-info').addClass('label-success');
        } else {
            $("#bt-github").find('.label').removeClass('label-info').addClass('label-default');
        }

        // --- Upman processing ---
        var currentUm = $("#bt-current").text().replace('v', '').trim();
        gitUm = data.upman_version.replace('v', '').trim();

        $("#upman-github").html('<span class="label label-info">v' + gitUm + '</span>');

       if (isNewerVersion(currentUm, gitUm))
       {
            $("#btn-download-um").prop("disabled", false);
            $("#upman-github").find('.label').removeClass('label-info').addClass('label-success');
        } else {
            $("#upman-github").find('.label').removeClass('label-info').addClass('label-default');
        }

    }).fail(function() {
        $("#radio-github, #bt-github").html('<span class="label label-danger">Connection error</span>');
    });

    // --- 3. Action buttons
    $("#btn-update-radio").click(function(e) {
        e.preventDefault();
        if (radioBinFile === "")
        {
          alert_("No update file available!");
        }
        else
        {
          updateRadio(gitRadio, radioBinFile);
        }
    });

    $("#btn-update-bt").click(function(e) {
        e.preventDefault();
        if (btBinFile === "") {
            alert_("No update file available!");
        }
        else
        {
          updateBluetooth(gitBt, btBinFile);
        }
    });

    $("#btn-backup-radio").click(function(e) {
        e.preventDefault();
        var currentVer = $("#radio-current").text().trim();
        downloadBackupWithProgress("/download_radio", "radio_" + currentVer + ".bin");
    });

    $("#btn-backup-bt").click(function(e) {
        e.preventDefault();
        var currentVer = $("#bt-current").text().trim();
        downloadBackupWithProgress("/download_bluetooth", "bluetooth_" + currentVer + ".bin");
    });

    $("#btn-backup-um").click(function(e) {
        e.preventDefault();
        var currentVer = $("#upman-current").text().trim();
        downloadBackupWithProgress("/download_upman", "upman_" + currentVer + ".bin");
    });

$("#manual-file-input").change(function() {
    var fileInput = document.getElementById("manual-file-input");
    var btnFlash = $("#btn-manual-upload");

    if (fileInput.files.length > 0) {
        var file = fileInput.files[0];
        var fileName = file.name.toLowerCase();

        // 1. Extension check
        if (!fileName.endsWith('.bin'))
        {
            //alert("Invalid file type! Please select a valid compiled .bin file.");
            alert_("Invalid file type!\nPlease select a valid compiled .bin file.");
            fileInput.value = ""; // clear selection
            btnFlash.prop("disabled", true);
            return;
        }

        // Size check
        // value="RADIO" or value="BTLS" ?
        var appTarget = $("input[name='manualTarget']:checked").val();
        var currentPartitionLimit = 0;

        if (appTarget === "RADIO") {
            currentPartitionLimit = partsize0;
        } else if (appTarget === "BTLS") {
            currentPartitionLimit = partsize1;
        }

        // For sure
        if (!currentPartitionLimit || currentPartitionLimit === 0) {
            currentPartitionLimit = 3145728; // Fallback 3 MB
        }

        // Size check
        if (file.size > currentPartitionLimit) {
            var fileMB = Math.round(file.size / 1024 / 1024 * 100) / 100;
            var limitMB = Math.round(currentPartitionLimit / 1024 / 1024 * 100) / 100;
            
            //alert("Error: File is too large!\n\nSelected file: " + fileMB + " MB\nTarget partition limit: " + limitMB + " MB\n\nFlash operation aborted for device safety.");
            tooLarge(fileMB, limitMB);
            fileInput.value = ""; // Vyčistit výběr
            btnFlash.prop("disabled", true);
            return;
        }
        btnFlash.prop("disabled", false);
    } else {
        btnFlash.prop("disabled", true);
    }
});

// For sure:
$("input[name='manualTarget']").change(function() {
    $("#manual-file-input").trigger("change");
});

$("#btn-manual-upload").click(function(e) {
    e.preventDefault();
    
    
    var fileInput = document.getElementById("manual-file-input");
    if (fileInput.files.length === 0) {
        alert_("Please select a .bin file first!");
        return;
    }
    var file = fileInput.files[0];
    targetedVersion = getVersion(file.name);
    var appTarget = $("input[name='manualTarget']:checked").val();
    updateLocal(file.name, appTarget, file);
  });
  
  
  
}

var otaBuffer = null;
var currentOffset = 0;
const CHUNK_SIZE = 4096;

function sendNextOtaChunk() {
    if (!otaBuffer) return;

    if (currentOffset < otaBuffer.byteLength) {
        var byteLength = Math.min(CHUNK_SIZE, otaBuffer.byteLength - currentOffset);
        var chunk = otaBuffer.slice(currentOffset, currentOffset + byteLength);
        
        currentOffset += byteLength;
        
        // Progress calculation
        var progress = Math.round((currentOffset / otaBuffer.byteLength) * 100);
        
        $("#flash-progress-text").text("Flashing firmware: " + progress + "% (" + Math.round(currentOffset / 1024) + " KB / " + Math.round(otaBuffer.byteLength / 1024) + " KB)");
        $("#flash-progress-bar").css("width", progress + "%");
        
        websock.send(chunk); // Sending to ESP32
    } else {
        console.log("All data sent. Finalizing update...");
        
        $("#flash-progress-text").text("Verifying firmware integrity and updating binaries.json... Please wait.");
        $("#flash-progress-bar").removeClass('progress-bar-danger').addClass('progress-bar-success');
        websock.send(JSON.stringify({ 
            "command": "ota_end",
            "new_version": targetedVersion 
        }));

        otaBuffer = null;
        currentOffset = 0;
    }
}

function restartHeartbeat() {
    if (heartbeat_interval === null) {
        missed_heartbeats = 0;
        heartbeat_interval = setInterval(function() {
            try {
                missed_heartbeats++;
                if (missed_heartbeats >= 3)
                    throw new Error("Too many missed heartbeats.");
            }
            catch(e) {
                clearInterval(heartbeat_interval);
                heartbeat_interval = null;
                console.warn("Closing connection. Reason: " + e.message);
                getContent("#emptycontent");
                $("#ws-connection-status").slideDown();
                websock.close();
            }
        }, 5000);
        console.log("Heartbeat monitoring successfully resumed.");
    }
}

function startOtaUpdate(binFileName, appTarget) {
    // 1. Zastavíme heartbeat před startem flashování
    if (heartbeat_interval !== null) {
        clearInterval(heartbeat_interval);
        heartbeat_interval = null;
        missed_heartbeats = 0;
        console.log("OTA update started: Heartbeat monitoring temporarily disabled.");
    }
    
    $("#flash-progress-container").show();
    $("#flash-progress-bar").css("width", "0%").removeClass('progress-bar-success').addClass('progress-bar-danger');
    $("#flash-progress-text").text("Downloading firmware from server...");

    var repoUrl = "/Pako2/RadioESP32"; 
    // Base URL for download latest files from GitHub: 
    var downloadBaseUrl = "https://github.com" + repoUrl + "/releases/latest/download/"; // for live operation 
    var fullBinUrl = downloadBaseUrl + binFileName;
    var secureUrl = fullBinUrl + "?cb=" + new Date().getTime();
    
    console.log("Fetching binary from GitHub: " + secureUrl);
    
    fetch(secureUrl)
    .then(response => {
        if (!response.ok) throw new Error("HTTP error " + response.status);
        return response.arrayBuffer();
    })
    .then(arrayBuffer => {
        console.log("File loaded to browser memory. Size: " + arrayBuffer.byteLength + " bytes.");
        otaBuffer = arrayBuffer;
        currentOffset = 0;
        
        var originalOnMessage = websock.onmessage;
        
        websock.onmessage = function(event) {
            var reply = event.data;
            console.log("ESP32 Reply: " + reply);
            
            if (reply === "STATUS: READY_FOR_DATA" || reply === "STATUS: CHUNK_OK") {
                setTimeout(function() {
                    sendNextOtaChunk();
                }, 30);
            }
            else if (reply === "STATUS: SUCCESS") {
                // 1. Schováme progress bar, protože nahrávání skončilo
                $("#flash-progress-container").fadeOut(300, function() {
                    
                    // 2. Zobrazíme panel s odpočtem restartu
                    $("#reboot-countdown-container").fadeIn(300);
                    
                    // Inicializujeme startovní hodnotu odpočtu (např. 5 sekund)
                    var timeLeft = 5;
                    $("#countdown-number").text(timeLeft);

                    // 3. Spustíme vteřinový odpočet
                    var countdownTimer = setInterval(function() {
                        timeLeft--;
                        $("#countdown-number").text(timeLeft);

                        // Přidáme vizuální efekt - rotaci ikonky (volitelné)
                        $("#reboot-spinner").css("transform", "rotate(" + ((5 - timeLeft) * 72) + "deg)");

                        if (timeLeft <= 0) {
                            clearInterval(countdownTimer);
                            // Jakmile odpočet doběhne na nulu, stránku bezpečně obnovíme
                            window.location.reload();
                        }
                    }, 1000);
                });
                
                // Uvolníme odesílací handlery
                otaBuffer = null;
                currentOffset = 0;
                websock.onmessage = originalOnMessage;
            }

            else if (reply.startsWith("ERROR:")) {
                $("#flash-progress-container").hide();
                //alert("Update failed: " + reply);
                updateFailed(reply.substring(7));
                otaBuffer = null;
                currentOffset = 0;
                websock.onmessage = originalOnMessage;
                
                // --- DOPLNĚNO: Reset selhal na straně ESP32, vracíme hlídání ---
                restartHeartbeat(); 
            }
        };

        // Upravte tento řádek na konci startOtaUpdate:
websock.send(JSON.stringify({ 
    "command": "ota_start", 
    "target": appTarget,
    "size": arrayBuffer.byteLength // Přidáno: přesná velikost firmware v bajtech
}));

    })
    .catch(error => {
        $("#flash-progress-container").hide();
        //alert("Failed to read firmware file from GitHub: " + error.message);
        failedToReadFw(error.message);
        
        // --- DOPLNĚNO: Soubor se nepodařilo z PC vůbec stáhnout, vracíme hlídání ---
        restartHeartbeat();
    });
}

// Helper function for semantic version comparison(returns true if remote > local)
function isNewerVersion(local, remote)
{
    var localParts = local.split('.').map(Number);
    var remoteParts = remote.split('.').map(Number);

    for (var i = 0; i < Math.max(localParts.length, remoteParts.length); i++)
    {
        var localVal = localParts[i] || 0;
        var remoteVal = remoteParts[i] || 0;

        if (remoteVal > localVal) return true;
        if (remoteVal < localVal) return false;
    }
    return false;
}

function saveJSON(data, anchorElement, filename){
    data = JSON.stringify(data, null, 2);
    var blob = new Blob([data], {type: 'application/json;charset=utf-8'}),
        e    = document.createEvent('MouseEvents'),
        a = document.getElementById(anchorElement);
    a.download = filename;
    a.href = window.URL.createObjectURL(blob);
    a.dataset.downloadurl =  ['application/json;charset=utf-8', a.download, a.href].join(':');
    e.initMouseEvent('click', true, false, window, 0, 0, 0, 0, 0, false, false, false, false, 0, null);
    a.dispatchEvent(e);
}

function backupset() {
	saveJSON(config, "downloadSet", "radioesp32-settings.json");
}

function revcommit() {
	document.getElementById("jsonholder").innerText = JSON.stringify(config, null, 2);
	$("#revcommit").modal("show");
}

function uncommited() {
	$("#commit").fadeOut(200, function() {
		$(this).css("background", "gold").fadeIn(1000);
	});
	document.getElementById("commit").innerHTML = "<h6>You have uncommited changes, please click here to review and commit.</h6>";
	$("#commit").click(function() {
		revcommit();
		return false;
	});
}

function inProgress(callback) {
	$("body").load("radioesp32.html #progresscontent", function(responseTxt, statusTxt, xhr) {
		if (statusTxt === "success") {
			$(".progress").css("height", "40");
			$(".progress").css("font-size", "xx-large");
			var i = 0;
			var prg = setInterval(function() {
				$(".progress-bar").css("width", i + "%").attr("aria-valuenow", i).html(i + "%");
				i++;
				if (i === 101) {
					clearInterval(prg);
					var a = document.createElement("a");
					//a.href = "http://" + window.location.host
					a.href = "http://" + config.general.hostnm + ".local";
					a.innerText = "Try to reconnect ESP";
					document.getElementById("reconnect").appendChild(a);
					document.getElementById("reconnect").style.display = "block";
					document.getElementById("updateprog").className = "progress-bar progress-bar-success";
					document.getElementById("updateprog").innerHTML = "Completed";
				}
			}, 250);
			switch (callback) {
				case "commit":
					sendMessage(JSON.stringify(config));
					break;
				case "restart":
					sendMessage("{\"command\":\"restart\"}");
					break;
				default:
					break;
			}
		}
	}).hide().fadeIn();
}

function commit() {
	inProgress("commit");
}


function isVisible(e) {
  if (!e){return false;}
	return !!(e.offsetWidth || e.offsetHeight || e.getBoundingClientRect().Width);
}

function colorStatusbar(ref) {
	var percentage = ref.style.width.slice(0, -1);
	if (percentage > 50) {
		ref.className = "progress-bar progress-bar-success";
	} else if (percentage > 25) {
		ref.className = "progress-bar progress-bar-warning";
	} else {
		ref.class = "progress-bar progress-bar-danger";
	}
}

async function listStats() {
  partsize = ajaxobj.partsize;
  partsize0 = ajaxobj.partsize0;
  partsize1 = ajaxobj.partsize1;
	const chipelement = await waitForElementToExist('#chip');
	chipelement.innerHTML = ajaxobj.chipid;
	document.getElementById("model").innerHTML = ajaxobj.chipmodel;
	document.getElementById("cpu").innerHTML = ajaxobj.cpu + " Mhz";
	document.getElementById("uptime").innerHTML = ajaxobj.uptime;
	document.getElementById("heap").innerHTML = ajaxobj.heap + " Bytes";
	var totalheap = ajaxobj.totalheap;
	document.getElementById("heap").style.width = (ajaxobj.heap * 100) / totalheap + "%";
	colorStatusbar(document.getElementById("heap"));
	document.getElementById("flash").innerHTML = (ajaxobj.partsize-ajaxobj.sketchsize) + " Bytes";
	document.getElementById("flash").style.width = ((ajaxobj.partsize-ajaxobj.sketchsize) * 100) / ajaxobj.partsize + "%";
	colorStatusbar(document.getElementById("flash"));
	document.getElementById("flash0").innerHTML = (ajaxobj.partsize0-ajaxobj.sketchsize0) + " Bytes";
	document.getElementById("flash0").style.width = ((ajaxobj.partsize0-ajaxobj.sketchsize0) * 100) / ajaxobj.partsize0 + "%";
	colorStatusbar(document.getElementById("flash0"));
	document.getElementById("flash1").innerHTML = (ajaxobj.partsize1-ajaxobj.sketchsize1) + " Bytes";
	document.getElementById("flash1").style.width = ((ajaxobj.partsize1-ajaxobj.sketchsize1) * 100) / ajaxobj.partsize1 + "%";
	colorStatusbar(document.getElementById("flash1"));
	document.getElementById("spiffs").innerHTML = ajaxobj.availspiffs + " Bytes";
	document.getElementById("spiffs").style.width = (ajaxobj.availspiffs * 100) / ajaxobj.spiffssize + "%";
	colorStatusbar(document.getElementById("spiffs"));
	document.getElementById("psram").innerHTML = ajaxobj.availpsram + " Bytes";
	document.getElementById("psram").style.width = (ajaxobj.availpsram * 100) / ajaxobj.psramsize + "%";
	colorStatusbar(document.getElementById("psram"));
	document.getElementById("ssidstat").innerHTML = ajaxobj.ssid;
	document.getElementById("ip").innerHTML = ajaxobj.ip;
	document.getElementById("gate").innerHTML = ajaxobj.gateway;
	document.getElementById("mask").innerHTML = ajaxobj.netmask;
	document.getElementById("dns").innerHTML = ajaxobj.dns;
	document.getElementById("mac").innerHTML = ajaxobj.mac;
    version = ajaxobj.version;
	$("#mainver").text(version);
	$("#sver").text(version);
	if (ajaxobj.hasOwnProperty("battery"))
	{
	$("#batprogress").css('display','table-row');
	document.getElementById("battery").innerHTML = ajaxobj.battery + " %";
  document.getElementById("battery").style.width = ajaxobj.battery + "%";
	colorStatusbar(document.getElementById("battery"));
	if (config.general.lowbatt)
	{
	  if (config.general.critbatt >= ajaxobj.battery)
	  {
	    $("#lowbatt").modal("show");
	  }
	}
	}
	else
	{
	  $("#batprogress").css('display','none');
	}

}



function getContent(contentname) {
	$("#dismiss").click();
	$(".overlay").fadeOut().promise().done(function() {
		var content = $(contentname).html();
		$("#ajaxcontent").html(content).promise().done(function() {
			switch (contentname) {
				case "#statuscontent":
					//listStats();
					break;
				case "#backupcontent":
					break;
				default:
					break;
			}
			$("[data-toggle=\"popover\"]").popover({
				container: "body"
			});
			$(this).hide().fadeIn();
		});
	});
}

function restoreSet() {
	var input = document.getElementById("restoreSet");
	var reader = new FileReader();
	if ("files" in input) {
		if (input.files.length === 0) {
			alert_("You did not select file to restore!");
		} else {
			reader.onload = function() {
				var json;
				try {
					json = JSON.parse(reader.result);
				} catch (e) {
					alert_("Not a valid backup file!");
					return;
				}
				if (json.command === "configfile")
				{
				  restoreConfig(json);
				}
			};
			reader.readAsText(input.files[0]);
		}
	}
}

function waitForElementToExist(selector) {
  return new Promise(resolve => {
    if (document.querySelector(selector)) {
      return resolve(document.querySelector(selector));
    }
    const observer = new MutationObserver(() => {
      if (document.querySelector(selector)) {
        resolve(document.querySelector(selector));
        observer.disconnect();
      }
    });
    observer.observe(document.body, {
      subtree: true,
      childList: true,
    });
  });
}

function twoDigits(value) {
	if (value < 10) {
		return "0" + value;
	}
	return value;
}

function restartESP() {
	inProgress("restart");
}

function socketMessageListener(evt) {
    var obj;
    try {
        obj = JSON.parse(evt.data);
    } catch (e) {
      console.log("No JSON: ",evt.data);
        return; // error in the above string (in this case, yes)!
    }
	if (obj.hasOwnProperty("command")) {
	  //console.log("\ncommand: ",obj.command);
		switch (obj.command) {
			case "status":
				ajaxobj = obj;
				console.log(JSON.stringify(obj)); //keep permanently!
				getContent("#statuscontent");
				listStats();
				break;
			case "configfile":
				config = obj;
				config.default=false;
				break;
			case "heartbeat":
			  // reset the counter for missed heartbeats
        missed_heartbeats = 0;
				break;
      case "binaries":
        listupdmanager(obj);
        break;
      case "jump":
        getContent("#jump"+obj.jump.toString()+"content");
        break;
      case "battlow":
        if (obj.value)
        {$("#lowbatt").modal("show");}
        else
        {
          $("#lowbatt").modal("hide");
          sendMessage("{\"command\":\"status\"}");
        }
        break;
      default:
				break;
		}
	}
	if (obj.hasOwnProperty("resultof")) {
		switch (obj.resultof) {
			default: break;
		}
	}
}

function pwoff()
{
  getContent("#pwoffcontent");
    setTimeout(function() {
    sendMessage("{\"command\":\"shutdown\"}");
    }, 2000);
  return false;	
}

$("#dismiss, .overlay").on("click", function() {
	$("#sidebar").removeClass("active");
	$(".overlay").fadeOut();
});

$("#sidebarCollapse").on("click", function() {
	$("#sidebar").addClass("active");
	$(".overlay").fadeIn();
	//$(".collapse.in").toggleClass("in");
	$("a[aria-expanded=true]").attr("aria-expanded", "false"); //mozna prop misto attr ?
});

$("#status").click(function() {
  //$('.collapse').collapse('hide');
  $("li.active").removeClass("active");
  $("#status").parent().addClass("active");
	sendMessage("{\"command\":\"status\"}");
	return false;
});

//$("#dropdown").click(function() {
//  $("li.active").removeClass("active");
//	return true;
//});

$("#updman").click(function() {
  $("li.active").removeClass("active");
  $("#updman").parent().addClass("active");
	getContent("#updatecontent");
	sendMessage("{\"command\":\"binaries\"}");
  return false;
});

$("#jump1").click(function() {
  $("li.active").removeClass("active");
  $("#jump1").parent().addClass("active");
	getContent("#jump1content");
    setTimeout(function() {
    sendMessage("{\"command\":\"jump\",\"target\":1}");
    }, 2000);
  return false;
});

$("#jump2").click(function() {
  $("li.active").removeClass("active");
  $("#jump2").parent().addClass("active");
	getContent("#jump2content");
    setTimeout(function() {
    sendMessage("{\"command\":\"jump\",\"target\":2}"); 
        // We will try to close the window (if the browser allows it)
        window.close();
    }, 500);
	return false;
});

$("#backup").click(function() {
  //$('.collapse').collapse('hide');
  $("li.active").removeClass("active");
  $("#backup").parent().addClass("active");
	getContent("#backupcontent");
	return false;
});

$("#pwoffreq").click(function() {
  $("li.active").removeClass("active");
  $("#pwoffreq").parent().addClass("active");
	$("#pwoff").modal("show");
	return false;
});

//$(".noimp").on("click", function() {
//	$("#noimp").modal("show");
//});

var xDown = null;
var yDown = null;

function handleTouchStart(evt) {
	xDown = evt.touches[0].clientX;
	yDown = evt.touches[0].clientY;
}

function handleTouchMove(evt) {
	if (!xDown || !yDown) {
		return;
	}

	var xUp = evt.touches[0].clientX;
	var yUp = evt.touches[0].clientY;

	var xDiff = xDown - xUp;
	var yDiff = yDown - yUp;

	if (Math.abs(xDiff) > Math.abs(yDiff)) { /*most significant*/
		if (xDiff > 0) {
			$("#dismiss").click();
		} else {
			$("#sidebarCollapse").click();
			/* right swipe */
		}
	} else {
		if (yDiff > 0) {
			/* up swipe */
		} else {
			/* down swipe */
		}
	}
	/* reset values */
	xDown = null;
	yDown = null;
}


// Make the function wait until the connection is made...
function waitForSocketConnection(socket, callback){
    setTimeout(
        function () {
            if (socket.readyState === 1) {
                if (callback != null){
                    callback();
                }
            } else {
                waitForSocketConnection(socket, callback);
            }

        }, 5); // wait 5 milisecond for the connection...
}

function sendMessage(msg){
    // Wait until the state of the socket is not ready and send the message when it is...
    waitForSocketConnection(websock, function(){
        websock.send(msg);
    });
}

function connectWS() {
	if (wsConnectionPresent) {
		return;
	}
	if (window.location.protocol === "https:") {
		wsUri = "wss://" + window.location.hostname + ":" + window.location.port + "/ws";
	} else if (window.location.protocol === "file:" || ["0.0.0.0", "localhost", "127.0.0.1"].includes(window.location.hostname)) {
		wsUri = "ws://localhost:8080/ws";
	}
	websock = null;
	websock = new WebSocket(wsUri);
	websock.addEventListener("message", socketMessageListener);


websock.onopen = function(evt) {
    // ...
    // other code which has to be executed after the client
    // connected successfully through the websocket
    // ...
    
		if (!gotInitialData) {
			sendMessage("{\"command\":\"getconf\"}");
			gotInitialData = true;
		}
    if (heartbeat_interval === null) {
        missed_heartbeats = 0;
        heartbeat_interval = setInterval(function() {
            try {
                missed_heartbeats++;
                if (missed_heartbeats >= 3)
                    throw new Error("Too many missed heartbeats.");
            }
            catch(e) {
                clearInterval(heartbeat_interval);
                heartbeat_interval = null;
                console.warn("Closing connection. Reason: " + e.message);
                getContent("#emptycontent");
                $("#ws-connection-status").slideDown();
                websock.close();
            }
        }, 5000);
    }
    $("#ws-connection-status").slideUp(1000, function() {
//     sendMessage("{\"command\":\"pmode\"}"); 
     sendMessage("{\"command\":\"status\"}"); 
    });
};

	websock.onclose = function(evt) {
	  websock = null;
	  gotInitialData = false;
	  heartbeat_interval = null
	  document.location = "index.html";
	};
}

function downloadupdate(app)
{
  var updurl = urls[app];
  if (updurl)
  {
  fnix = updurl.lastIndexOf("/") + 1,
  filename = updurl.substr(fnix);
  //$('#update').modal('hide');
  var link = document.createElement("a");
  link.download = filename;
  link.target = "_blank";
  link.href = updurl;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  delete link;
  }
}

function start() {
	radioesp32 = document.createElement("div");
	radioesp32.id = "mastercontent";
	radioesp32.style.display = "none";
	document.body.appendChild(radioesp32);
//  connectWS();
	$("#mastercontent").load("radioesp32.html", function(responseTxt, statusTxt, xhr) {
		if (statusTxt === "success") {
			$("#signin").modal({
				backdrop: "static",
				keyboard: false
			});
			$("[data-toggle=\"popover\"]").popover({
				container: "body"
			});
		}
	});
}


function login() {
    var username = "admin";
    var password = document.getElementById("password").value;
    var url = "/login";
    var xhr = new XMLHttpRequest();
    xhr.open("get", url, true, username, password);
    xhr.onload = function(e) {
      if (xhr.readyState === 4) {
        if (xhr.status === 200) {
          $("#signin").modal("hide");
          connectWS();
        } else {
          alert_("Incorrect password !");
        }
      }
    };
    xhr.send(null);
}



document.addEventListener("touchstart", handleTouchStart, false);
document.addEventListener("touchmove", handleTouchMove, false);
