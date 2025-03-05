var version = "";
var PASSW = "";
var LOGIN = "";
var updurl = "";
var DISP = false;
var BATTERY = false;
var MINPWOFF = 5;
var MAXPWOFF = 100;
var pwofftime = 0;
var AUTOSHUTDOWN = false;
var SDCARD = false;
var SDREADY = false;
var PM_RADIO = true;
var TRIX=-1;
var TRACKS=null;
var id=null;
var station;
var rowsnum;
var album;
var artist;
var title;
var muteval;
var volumeval;
var random = false;
var loop = true;
var presetmax = 99;
var irmax = 100;
var netwmax = 8;

var websock = null;
var heartbeat_msg = '--heartbeat--', heartbeat_interval = null, missed_heartbeats = 0;
var wsUri = "ws://" + window.location.host + "/ws";
var utcSeconds;
var tzoffset;
//var data = [];
var ajaxobj;
var netw = {"ipaddress": "", "subnet": "", "gateway": "", "dnsadd": "", "dhcp": 1};
var ipdict = {"ipaddress": "IP Address", "subnet": "Mask", "gateway": "Gateway", "dnsadd": "DNS Server"};
var IRCOMMANDS;
const GPIOS = [2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33];//
const GPIOSI = [34,35,36,39];//
var RESERVEDGPIOS=[];
var config = {};
var backupstarted = false;
var restorestarted = false;
var gotInitialData = false;
var wsConnectionPresent = false;
var running = null;
var duration = null;
var mp3list_req = false;
var radioesp32;

// define a function that converts a string to hex
const stringToHex = (str) => {
  let hex = '';
  for (let i = 0; i < str.length; i++) {
    const charCode = str.charCodeAt(i);
    const hexValue = charCode.toString(16);

    // Pad with zeros to ensure two-digit representation
    hex += hexValue.padStart(2, '0');
  }
  return hex;
};

function defvolume(val)
{
  $('#defvol').val(val);
  $('#volume2').html(val);
}

function seekstep(val)
{
  $('#seekstep').val(val);
  $('#seekstep2').html(val);
}

function volume(val, source)
{
  $('#volume').val(val);
  $('#volume1').html(val);
  if (source===1)
  //{websock.send("{\"command\":\"volume\",\"volume\":"+val+"}");}
  {sendMessage("{\"command\":\"volume\",\"volume\":"+val+"}");}
}

function toHMS(secs, hr=false)
{
  var HH = `${Math.floor(secs / 3600)}`.padStart(2, '0');
  var MM = `${Math.floor(secs / 60) % 60}`.padStart(2, '0');
  var SS = `${Math.floor(secs % 60)}`.padStart(2, '0');
  var result = [MM, SS].join(':');
  if (hr)
  {
    result = MM+":"+result;
  }
  return result;
}

function toHHMMSS(secs)
{
  var hr=duration>3600;
  return toHMS(secs,hr)+" / "+toHMS(duration,hr);
}

function handleProgress(val, source = 0)
{
  $('#progress').val(val);
  var pos = parseInt(duration*(val/1000));
  $('#progress1').html(toHHMMSS(pos));
  if (source===1)
  {sendMessage("{\"command\":\"position\",\"position\":"+pos+"}");}
}

function autoshutdown(val)
{
  $('#asd').val(val);
  $('#asd1').html(val);
}

function defautoshutdown(val)
{
  $('#dasd').val(val);
  $('#dasd1').html(val);
}


function stepitem(val)
{
  if (PM_RADIO)
 	{
  sendMessage("{\"command\":\"steppreset\",\"val\":"+val+"}");
 	}
 	else
 	{
  sendMessage("{\"command\":\"steptrack\",\"val\":"+val+"}");
 	}
}

function seek(val)
{
  sendMessage("{\"command\":\"seek\",\"val\":"+val+"}");
}

function handlerndloop(objct)
{
    if (random)
    {
      $('#rnd').removeClass('btn-success');
      $('#loop').prop('disabled', true);
    }
    else
    {
      $('#rnd').addClass('btn-success');
      $('#loop').prop('disabled', false);
      if (loop)
      {
        $('#loop').removeClass('btn-success');
      }
      else
      {
        $('#loop').addClass('btn-success');
      }      
    }
}

function rndloop(val)
{
  sendMessage("{\"command\":\"rndloop\",\"val\":"+val+"}");
}

function playpause()
{
  sendMessage("{\"command\":\"playpause\"}");
}

function stop()
{
  sendMessage("{\"command\":\"stop\"}");
}

function shutdown(val)
{
  if (val==1)
  {
    sendMessage("{\"command\":\"shutdown\"}");
  }
  else if (val===0)
  {
    $("#schedpwoff").prop('disabled', false);
    $("#cancpwoff").prop('disabled', true);
    pwofftime=0;
    clearInterval(id);
    sendMessage("{\"command\":\"schedpwoff\",\"val\":"+val+"}");
    $("#asd5").css('display','none');    
    $("#asd3").css('display','block');
  }
  else if (val==-1)
  {
    var mins=$('#asd').val();
    $("#schedpwoff").prop('disabled', true);
    $("#cancpwoff").prop('disabled', false);
    sendMessage("{\"command\":\"schedpwoff\",\"val\":"+mins+"}");
    
    var asdx = document.getElementById("asd3");
    if (isVisible(asdx))
    {
      $("#asd3").css('display','none');
      $("#asd5").css('display','block');    
      var c = new Date();
	    pwofftime = 60*mins+Math.floor((c.getTime() / 1000));      
	    id=setInterval(myTimer,1000,id);
    }
  }
}

function volumeChange(val)
{
  var step = 4;
  var vol = parseInt($('#volume').val());
  if (val===1)
  {
    vol = vol+step;
    if (vol>100)
    {
      vol = 100;
    }
  }
  else
  {
    vol = vol-step;
    if (vol<0)
    {
      vol = 0;
    }
  }
  volume(vol, 1);
}

function mute(val, source)
{
  var mutebut = $("#mute");
  var unmutebut = $("#unmute");
  if (val)
  {
     mutebut.css('display','none');
     unmutebut.css('display','block');
  }
  else
  {
     mutebut.css('display','block');
     unmutebut.css('display','none');
  }
  if (source===1)
  {sendMessage("{\"command\":\"mute\",\"mute\":"+val+"}");}
}

function browserTime() {

	var d = new Date(0);
	var c = new Date();
	var timestamp = Math.floor((c.getTime() / 1000) + ((c.getTimezoneOffset() * 60) * -1));
	d.setUTCSeconds(timestamp);
	document.getElementById("rtc").innerHTML = d.toUTCString().slice(0, -3);
}

function deviceTime() {
	var t = new Date(0); // The 0 there is the key, which sets the date to the epoch,
	var devTime = Math.floor(utcSeconds + (tzoffset * 60));
	t.setUTCSeconds(devTime);
	document.getElementById("utc").innerHTML = t.toUTCString().slice(0, -3);
}

function syncBrowserTime() {
	var d = new Date();
	var timestamp = Math.floor((d.getTime() / 1000));
	var datatosend = {};
	datatosend.command = "settime";
	datatosend.epoch = timestamp;

	const select = document.getElementById("DropDownTimezone");
	config.ntp.tzname = select.options[select.selectedIndex].text;
	config.ntp.timezone = select.value;

	datatosend.timezone = config.ntp.timezone;
	sendMessage(JSON.stringify(datatosend));
  setTimeout(() => {  $("#ntp").click(); }, 500);
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

function createOptions(slct, input)
{
  slct.children().remove();
  slct.append($("<option>").attr('value',255).text("None"));
  $(GPIOS).each(function()
  {
    if (!RESERVEDGPIOS.includes(parseInt(this)))
    {
      slct.append($("<option>").attr('value',this).text(["GPIO-",String(this).padStart(2, '0')].join('')));
    }
  });
  if (input)
  {
    $(GPIOSI).each(function()
    {
      if (!RESERVEDGPIOS.includes(parseInt(this)))
      {
        slct.append($("<option>").attr('value',this).text(["GPIO-",String(this).padStart(2, '0')].join('')));
      }
    });
  }
}

function handleDsptype(val)
{
  var vistft='none';
  if (val==128)
  {
    vistft='block';
  }
  $(".tftelm").css('display',vistft);
}

function listhardware() {
	if (DISP)
  {
    $("#dspdiv1").css('display','block');
    $("#dspdiv2").css('display','block');
    handleDsptype(config.hardware.dsptype);
    $("select[name=dsptype]").prop('disabled', true);
	  $('#bckinv'+config.hardware.bckinv.toString()).prop('checked', true);
	  $('#angle'+config.hardware.angle.toString()).prop('checked', true);
    var bckpin = $('#bckpin');
	  createOptions(bckpin, false);
	  bckpin.val(config.hardware.bckpin);
  }
  else
  {
    $("#dspdiv1").css('display','none');
    $("#dspdiv2").css('display','none');
	  $("select[name=dsptype]").val(config.hardware.dsptype).change();
  }
  if (SDCARD)
  {
    $("#pinsdd").css('display','block');
	  var sddsel = $('#sddpin');
	  createOptions(sddsel, false);
	  sddsel.val(config.hardware.sddpin);
	  $('#sdpull'+config.hardware.sdpullup.toString()).prop('checked', true);
  }
  else
  {
    $("#pinsdd").css('display','none');
  }
  if (AUTOSHUTDOWN)
  {
    var onoffisel = $('#onoffipin');
	  createOptions(onoffisel, true);
	  onoffisel.val(config.hardware.onoffipin);
    var onoffosel = $('#onoffopin');
	  createOptions(onoffosel, false);
	  onoffosel.val(config.hardware.onoffopin);
	  $("#asddiv").css('display','block');
  }
  else
  {
	  $("#asddiv").css('display','none');
  }
	$('#extpull'+config.hardware.extpullup.toString()).prop('checked', true);
	var bclksel = $('#bclkpin');
	createOptions(bclksel, false);
	bclksel.val(config.hardware.bclkpin);
	var doutsel = $('#doutpin');
	createOptions(doutsel, false);
	doutsel.val(config.hardware.doutpin);
	var wssel = $('#wspin');
	createOptions(wssel, false);
	wssel.val(config.hardware.wspin);
	var encclksel = $('#encclkpin');
	createOptions(encclksel, true);
	encclksel.val(config.hardware.encclkpin);
	var encdtsel = $('#encdtpin');
	createOptions(encdtsel, true);
	encdtsel.val(config.hardware.encdtpin);
	var encswsel = $('#encswpin');
	createOptions(encswsel, true);
	encswsel.val(config.hardware.encswpin);
  var irsel = $('#irpin');
	createOptions(irsel, true);
	irsel.val(config.hardware.irpin);
  var mutesel = $('#mutepin');
	createOptions(mutesel, false);
	mutesel.val(config.hardware.mutepin);
  var spicssel = $('#spicspin');
	createOptions(spicssel, false);
	spicssel.val(config.hardware.spicspin);
  var spidcsel = $('#spidcpin');
	createOptions(spidcsel, false);
	spidcsel.val(config.hardware.spidcpin);
}

function savehardware() {
  config.hardware.extpullup = parseInt(	$(':radio[name="extpullup"]:checked').val());
  if (DISP)
  {
  config.hardware.bckinv = parseInt(	$(':radio[name="bckinv"]:checked').val());
  config.hardware.angle = parseInt(	$(':radio[name="angle"]:checked').val());
  config.hardware.dsptype = parseInt(	$('#dsptype').val());
  config.hardware.bckpin = parseInt($("#bckpin").val());
  }
  if (SDCARD)
  {
    config.hardware.sdpullup = parseInt(	$(':radio[name="sdpullup"]:checked').val());
	  config.hardware.sddpin = parseInt($("#sddpin").val());
  }
  if (AUTOSHUTDOWN)
  {
	  config.hardware.onoffipin = parseInt($("#onoffipin").val());
	  config.hardware.onoffopin = parseInt($("#onoffopin").val());
  }
	config.hardware.bclkpin = parseInt($("#bclkpin").val());
	config.hardware.doutpin = parseInt($("#doutpin").val());
	config.hardware.wspin = parseInt($("#wspin").val());
	config.hardware.encclkpin = parseInt($("#encclkpin").val());
	config.hardware.encdtpin = parseInt($("#encdtpin").val());
	config.hardware.encswpin = parseInt($("#encswpin").val());
	config.hardware.irpin = parseInt($("#irpin").val());
	config.hardware.mutepin = parseInt($("#mutepin").val());
	config.hardware.spicspin = parseInt($("#spicspin").val());
	config.hardware.spidcpin = parseInt($("#spidcpin").val());
	uncommited();
}

function listntp() {
	sendMessage("{\"command\":\"gettime\"}");
	document.getElementById("ntpserver").value = config.ntp.server;
	document.getElementById("intervals").value = config.ntp.interval;
	const select = document.getElementById("DropDownTimezone");
	for (var option of select.options) {
		if (option.text === config.ntp.tzname) {
			option.selected = true;
			break;
		}
	}
	browserTime();
	deviceTime();
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

function getadcbat(val)
{
  sendMessage("{\"command\":\"getadcbat\",\"val\":\""+val+"\"}");
  return false; //don't close window !
}

function teststation(url)
{
  sendMessage("{\"command\":\"test\",\"url\":\""+url+"\"}");
  return false; //don't close window !
}

function initDefstatSelect()
{
  $("#defstat").children().remove();
  const select = document.getElementById("defstat");
  var opt = document.createElement("option");
  opt.value = 0;
  opt.textContent = "[00]\xa0\xa0Random";
  select.appendChild(opt);
    var radstarr = [];
    var ft = FooTable.get("#presettable");
    $.each(ft.rows.all, function (i, row) {
        v = row.val();
        values = {
            preset: v.preset,
            name: v.name,
            url: v.url
        };
        radstarr.push(values);
    });
    radstarr.sort(function(a, b)
    {
       return a.preset - b.preset;
    }); 
for (var i = 0; i < radstarr.length; i++)
	{
		var opt = document.createElement("option");
		opt.value = parseInt(radstarr[i].preset);
		opt.name = radstarr[i].name;
		opt.textContent =  ["[",String(opt.value).padStart(2, '0'),"]\xa0\xa0",opt.name].join('');
		opt.url = radstarr[i].url;
		select.appendChild(opt);
	}
	select.value = config.radio.defstat;  
}

function listsd()
{
  $('#seekstep').val(config.sdplayer.seekstep);
  $('#seekstep2').html(config.sdplayer.seekstep);
}

function listradio()
{
  $('#defvol').val(config.radio.defvol);
  $('#bass').val(config.radio.bass);
  $('#mid').val(config.radio.mid);
  $('#treble').val(config.radio.treble);
  $('#volume2').html(config.radio.defvol);
  $('#lowpass').html(config.radio.bass);
  $('#bandpass').html(config.radio.mid);
  $('#highpass').html(config.radio.treble);
  var raddata = config.radio.stations;
  initStatsTable(raddata);
  initDefstatSelect();
}

function listirremote()
{
  var irdata = JSON.parse(JSON.stringify(config.irremote.commands));
  for (i = 0; i < irdata.length; i++)
  {
    irdata[i].ircmdtxt = IRCOMMANDS[irdata[i].ircmd];
    irdata[i].ircodetxt = irdata[i].ircode.toString(16).toUpperCase().padStart(6, 0);
  }
  initRemoteTable(irdata);
}

function saveirremote() {
    config.irremote.commands = readRemoteTable();
    uncommited();
}

function handlePlayPause(rnnng)
{
  if (rnnng != running)
  {
    if (rnnng)
    {
      $('#playpause').find('span').removeClass('glyphicon-play').addClass('glyphicon-pause');
    }
    else
    {
      $('#playpause').find('span').removeClass('glyphicon-pause').addClass('glyphicon-play');
    }
    running = rnnng;
  }
}

function handleadcbat(obj)
{
  $('#'+obj.kind).val(obj.val);
}

function handleSDstate(obj)
{
  var pos;
  handlePlayPause(obj.running);
  if (!PM_RADIO)
  {
    if (obj.duration>0)
    {
      duration = obj.duration;
      pos = parseInt(obj.position*(1000/obj.duration));
    }
    else
    {
      pos = 0;
    }
 	  $('#progress').val(pos);
    $('#progress1').html(toHHMMSS(obj.position));
  }
}

function savesd()
{
  config.sdplayer.seekstep = parseInt($('#seekstep').val());
  uncommited();
}

function saveradio() {
	  config.radio.defstat = parseInt($('#defstat').val());
    config.radio.stations = readStatsTable();
    config.radio.defvol = $('#defvol').val();
    config.radio.bass = $('#bass').val();
    config.radio.mid = $('#mid').val();
    config.radio.treble = $('#treble').val();
    uncommited();
}


function saventp() {
	config.ntp.server = document.getElementById("ntpserver").value;
	config.ntp.interval = parseInt(document.getElementById("intervals").value);
	const select = document.getElementById("DropDownTimezone");
	config.ntp.tzname = select.options[select.selectedIndex].text;
	config.ntp.timezone = select.value;
	uncommited();
}

function savegeneral() {
	config.general.hostnm = document.getElementById("hostname").value;
	if (AUTOSHUTDOWN)
	{
	  config.general.dasd = parseInt(document.getElementById("dasd").value);
	}
	if (BATTERY)
	{
	  config.general.bat0 = parseInt(document.getElementById("bat0").value);
	  config.general.bat100 = parseInt(document.getElementById("bat100").value);
	  config.general.lowbatt = parseInt($(':radio[name="lowbatt"]:checked').val());
    config.general.critbatt = parseInt($('#critbatt').val());
	}
	uncommited();
}

function savedisplay() {
  if (DISP)
  {
	config.display.backlight1 = parseInt(document.getElementById("backlight1").value);
	config.display.backlight2 = parseInt(document.getElementById("backlight2").value);
  }
	config.display.idle = parseInt(document.getElementById("idle").value);
	config.display.speed = parseInt(document.getElementById("speed").value);
	config.display.scrollgap = parseInt(document.getElementById("scrollgap").value);
	config.display.dateformat = document.getElementById("dateformat").value;
	config.display.monday = document.getElementById("day0").value.substring(0, 16);
	config.display.tuesday = document.getElementById("day1").value.substring(0, 16);
	config.display.wednesday = document.getElementById("day2").value.substring(0, 16);
	config.display.thursday = document.getElementById("day3").value.substring(0, 16);
	config.display.friday = document.getElementById("day4").value.substring(0, 16);
	config.display.saturday = document.getElementById("day5").value.substring(0, 16);
	config.display.sunday = document.getElementById("day6").value.substring(0, 16);
  config.display.tmode = parseInt($(':radio[name="timeoutmode"]:checked').val());
	config.display.sdclock = parseInt($(':radio[name="sdclockenabled"]:checked').val());
	if (document.getElementById("calon").checked) {
		config.display.calendar = 1;
	} else {
		config.display.calendar = 0;
	}
	uncommited();
}

function checkOctects(input) {
	var ipformat = /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
	var call = document.getElementById(input);
	if (call.value.match(ipformat)) {
		return true;
	} else {
		alert("You have entered an invalid IP address on the field \"" + ipdict[input]+"\"");
		call.focus();
		return false;
	}
}

function savenetwork()
{
  config.network.apssid=$('#apssid').val();
  config.network.apaddress=$('#apaddress').val();
  config.network.apsubnet=$('#apsubnet').val();
  config.network.networks = JSON.parse(JSON.stringify(readNetwTable()));
  for (i = 0; i < config.network.networks.length; i++) {
    config.network.networks[i].wifipass=btoa(config.network.networks[i].wifipass);
  }
  uncommited();
}

function make_base_auth(user, password) {
  var tok = user + ':' + password;
  var hash = btoa(tok);
  return "Basic " + hash;
}

var formData = new FormData();

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
				case "upload":
					$.ajax({
						url: "/update",
						type: "POST",
						data: formData,
					  processData: false,
						contentType: false,
						beforeSend: function (xhr){ 
              xhr.setRequestHeader('Authorization', make_base_auth(LOGIN, atob(PASSW)));
              LOGIN = "";
              PASSW = "";
						}
					});
					break;
				case "commit":
					sendMessage(JSON.stringify(config));
					break;
				case "destroy":
					sendMessage("{\"command\":\"destroy\"}");
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

function handleDHCP(ntw) {
	if (ntw.dhcp === 0) {
		document.getElementById("ipaddress").value = ntw.ipaddress;
		document.getElementById("subnet").value = ntw.subnet;
		document.getElementById("dnsadd").value = ntw.dnsadd;
		document.getElementById("gateway").value = ntw.gateway;
		$("#staticip1").slideDown();
		$("#staticip1").show();
		$("#staticip2").slideDown();
		$("#staticip2").show();
	} else {
	  $(':radio[name="dhcpenabled"][value=1]').prop('checked', true);
		$("#staticip2").slideUp();
		$("#staticip1").slideUp();
	}
}

function handleDHCP0()
{
  var ntw = netw;
  ntw.dhcp = 0;
  handleDHCP(ntw);
}

function handleDHCP1()
{
  var ntw = netw;
  ntw.dhcp = 1;
  handleDHCP(ntw);
}

function formatDate(date, format) {
    const map = {
        mm: String(date.getMonth() + 1).padStart(2, '0'),
        dd: String(date.getDate()).padStart(2, '0'),
        yyyy: String(date.getFullYear()).padStart(4, '0')
    }
    return format.replace(/mm|dd|yyyy/gi, matched => map[matched])
}

//function GetFormatedDate(date, format) {
//	format.replace('mm', date.getMonth() + 1)
//    .replace('yyyy', date.getFullYear())
//	.replace('dd', date.getDate());
//	return format;
//}

function listnetwork() {
	$('#apssid').val(config.network.apssid);
  $('#apaddress').val(config.network.apaddress);
  $('#apsubnet').val(config.network.apsubnet);
  
  var netdata = JSON.parse(JSON.stringify(config.network.networks));

  for (i = 0; i < netdata.length; i++) {
    netdata[i].wifipass=atob(netdata[i].wifipass);
  }
    initNetwTable(netdata);
}

function getFormat()
{
  var select = document.getElementById("dateformat");
	var selected = select.options[select.selectedIndex];
	var value = selected.value;
}

function getStation()//on combo click
{
  var select = document.getElementById("stationlist");
	var selected = select.options[select.selectedIndex];
	var value = selected.value;
  if (PM_RADIO)	
  {
	var preset = parseInt(selected.preset);
	var url = selected.url;
  sendMessage("{\"preset\":" + preset + ",\"command\":\"preset\"}");
  }
  else
  {
    sendMessage("{\"track\":" + value + ",\"command\":\"track\"}");
  }
}

function formatTime(dur) {
  var hrs = ~~(dur / 3600);
  var mins = ~~((dur % 3600) / 60);
  var secs = ~~dur % 60;

  // Output like "1:01" or "4:03:59" or "123:03:59"
  var ret = "";
  if (hrs > 0) {
    ret += hrs + ":";
  }
  ret += String(mins).padStart(2, '0') + ":" + String(secs).padStart(2, '0');
  return ret;
}

function myTimer(id)
{
  if (pwofftime==0)
  {
    clearInterval(id);
  }
  else
  {
      var c = new Date();
	    var timestamp = Math.floor((c.getTime() / 1000));
	    var remtime=pwofftime-timestamp;
	    var asdtxt = document.getElementById("asd6");
	    if (remtime>0)
	    {
    	  if (isVisible(asdtxt))
        {
          asdtxt.textContent=formatTime(remtime);
        }
        else
        {
          clearInterval(id);
        }
	    }
	    else
	    {
	      if (isVisible(asdtxt))
        {
          asdtxt.textContent=formatTime(0);
        }
        clearInterval(id);
	    }
  }
}

function fillTracklist(select)
{
  var tracks = TRACKS.split ( "\n" ) ;
  for ( i = 0 ; i < ( tracks.length ) ; i++ )
  {
    strparts = tracks[i].split ( "/" ) ;
    opt = document.createElement( "OPTION" ) ;
    opt.value = i ;
    opt.text = strparts[strparts.length-1] ;
    select.add(opt) ;
  }
  select.selectedIndex = TRIX;
}

async function listcontrol(rnnng) {
 	const select = await waitForElementToExist('#stationlist');
 	$("#stationlist").children().remove();
 	
 	if (PM_RADIO)
 	{
 	 $("#comm1").text("Radio Control");
 	 document.getElementById("comm2").firstChild.data="Preset";
 	 $("#comm3").attr('data-content',"Selecting a radio station from the preset list");
  var stations = config.radio.stations;
  stations.sort(function(a, b)
    {
       return a.preset - b.preset;
    });
	for (var i = 0; i < stations.length; i++)
	{
		var opt = document.createElement("option");
		opt.preset = parseInt(stations[i].preset);
		opt.value = stations[i].name;
		opt.textContent =  ["[",String(opt.preset).padStart(2, '0'),"]\xa0\xa0",opt.value].join('');
		opt.url = stations[i].url;
		select.appendChild(opt);
	}
	$("#seekm").css('display','none');
	$("#seekp").css('display','none');
	$("#playpause").css('display','none');
	$("#stop").css('display','none');
	$("#prgrss").css('display','none');
	$("#rndloop").css('display','none');
	if (SDREADY && !TRACKS)
  {
    mp3list_req = true;
    $.get( //async !!!
    "/mp3list", {}, function(data)
    {
      TRACKS = data;
    });
    }	
 	}
 	else //SD_PLAYER
 	{
 	 $("#comm1").text("SD Player Control");
 	 document.getElementById("comm2").firstChild.data="Track";
 	 $("#comm3").attr('data-content',"Selecting a audio file from the track list");
  var i, opt, strparts;
  if (!mp3list_req)
  {
    mp3list_req = true;
    if (!TRACKS)
    {
      mp3list_req = true;
      $.get( //async !!!
      "/mp3list", {}, function(data)
      {
        TRACKS = data;
        fillTracklist(select);
      });
    }
    else
    {
      fillTracklist(select);
    }
  }
  else
  {
    if (TRACKS)
    {
      fillTracklist(select);
    }
  }
  $("#seekm").css('display','block');
	$("#seekp").css('display','block');
	$("#playpause").css('display','block');
	$("#stop").css('display','block');
	$("#prgrss").css('display','block');
	$("#rndloop").css('display','block');
  handlePlayPause(rnnng);
  handlerndloop();
 	}
	$('#volume').val(volumeval);
  $('#volume1').html(volumeval);
  if (AUTOSHUTDOWN)
  {
    $("#asd4").css('display','block');
    $('#asd').attr('min', MINPWOFF);
    $('#asd').attr('max', MAXPWOFF);
	  $('#asd').val(config.general.dasd);
    $('#asd1').html(config.general.dasd);
    if (pwofftime>0)
    {
      id=setInterval(myTimer,1000,id);
      $("#asd3").css('display','none');
      $("#asd5").css('display','block');
      $("#schedpwoff").prop('disabled', true);
      $("#cancpwoff").prop('disabled', false);
      //disp. asd5
    }
    else
    {
      $("#asd5").css('display','none');
      $("#asd3").css('display','block');
      $("#schedpwoff").prop('disabled', false);
      $("#cancpwoff").prop('disabled', true);
    }
  }
  else
  {
    $("#asd3").css('display','none');
    $("#asd4").css('display','none');
    $("#asd5").css('display','none');    
  }
  mute(muteval, 0);
  updateMetadata(0);
}


function readRemoteTable() {
    var irarr = [];
    var ft = FooTable.get("#irtable");
    $.each(ft.rows.all, function (i, row) {
        v = row.val();
        values = {
            descr: v.descr.substring(0, 24),
            ircmd: v.ircmd,
            ircode: v.ircode
        };
        irarr.push(values);
    });
    return irarr;
}

function readStatsTable() {
    var radstarr = [];
    var ft = FooTable.get("#presettable");
    $.each(ft.rows.all, function (i, row) {
        v = row.val();
        values = {
            preset: v.preset,
            name: v.name,
            url: v.url
        };
        radstarr.push(values);
    });
    return radstarr;
}

function limitTable(tbl, limit)
{
   $(".footable-add").prop("disabled", tbl.rows.all.length>=limit);
}


function isNotUnique(tbl, val)
{
  var result = false;
  tbl.rows.all.every(function(row) {
  if (row.val().preset === parseInt(val))
  {
    result = true; // ~ break
    return false;
  }
  return true;     // ~ continue
});
return result;
}

function isNotUnique2(tbl, val)
{
  var result = false;
  tbl.rows.all.every(function(row) {
  if (row.val().ircode === parseInt(val))
  {
    result = true; // ~ break
    return false;
  }
  return true;     // ~ continue
});
return result;
}

function free(array, start) {
    array.sort(function(a, b)
    {
       return a - b;
    });
    array.every(function (a) {
        if (start === a) {
            start = a + 1;
            return true;
        }
    });
    return start;
}

function fillNumber(tbl)
{   var prarr = [];
    $.each(tbl.rows.all, function (i, row) {
        prarr.push(parseInt(row.val().preset));
    });
    $("#preset").val(free(prarr, 1));
}

function initStatsTable(vals) {
    jQuery(function ($) {
        var $modal = $("#editor-modal"),
            $editor = $("#editor"),
            $editorTitle = $("#editor-title"),
            ft = window.FooTable.init("#presettable", {
                columns: [{
                    "name": "preset",
                    "title": "Preset Nr.",
                    "type": "number",
                },
                {
                    "name": "name",
                    "title": "Station name",
                    "type": "text",
                },
                {
                    "name": "url",
                    "title": "Stream url",
                    "type": "text",
                },
                ],
                rows: vals,
                editing: {
                    showText: "<span class=\"fooicon fooicon-pencil\" aria-hidden=\"true\"></span> Edit Stations",
                    addText: "New Station",
                    addRow: function () {
                        $editor[0].reset();
                        $editorTitle.text("Add a new Station");
                        $modal.modal("show");
                        fillNumber(ft);
                    },
                    editRow: function (row) {
                        var values = row.val();
                        $editor.find("#preset").val(values.preset);
                        $editor.find("#name").val(values.name);
                        $editor.find("#url").val(values.url);
                        $modal.data("row", row);
                        $editorTitle.text("Edit Station # " + values.name);
                        $modal.modal("show");
                        //limitTable(ft, presetmax);
                    },
                    deleteRow: function (row) {
                        var name = row.value.name;
                        if (confirm("This will remove station \"" + name + "\" from database. Are you sure?")) {
                            var jsontosend = "{\"name\":\"" + name + "\",\"command\":\"remove\"}";
                            sendMessage(jsontosend);
                            row.delete();
                            limitTable(ft, presetmax);
                        }
                    }
                },
            });
            ft.pageSize(8);
        $editor.on("submit", function (e) {
            if (this.checkValidity && !this.checkValidity()) {
                return;
            }
            e.preventDefault();
            var row = $modal.data("row"),
                values = {
                    preset: $editor.find("#preset").val(),
                    name: $editor.find("#name").val(),
                    url: $editor.find("#url").val()
                };
              var val = $editor.find("#preset").val();
            if (row instanceof window.FooTable.Row)
            {
              if ((row.val().preset==val) || !isNotUnique(ft, val))
              {
                row.delete();
	            }
	            else
	            {
              alert("The preset with the number \"" + val + "\" already exists in the table!");
              return; // stay in the editor!
	            }
            }
            else
            {
              if (isNotUnique(ft, val))
              {
              alert("The preset with the number \"" + val + "\" already exists in the table!");
              return; // stay in the editor!
              }
            }
            ft.rows.add(values);
            $modal.modal("hide");
            limitTable(ft, presetmax);
            var sorting = ft.use(FooTable.Sorting);
            sorting._sort(0, 0);
            initDefstatSelect();
        });
        limitTable(ft, presetmax);
    });
}

function initRemoteTable(vals) {
    jQuery(function ($) {
        var $modal = $("#editor-modal3"),
            $editor = $("#editor3"),
            $editorTitle = $("#editor-title3"),
            ft = window.FooTable.init("#irtable", {
                columns: [
                {
                    "name": "descr",
                    "title": "Description",
                    "type": "text",
                    "sorted": true,
                    "direction": "ASC"
                },
                {
                    "name": "ircode",
                    "title": "IR Code",
                    "type": "number",
                    "visible":false
                },
                {
                    "name": "ircodetxt",
                    "title": "IR Code",
                    "type": "text",
                    "style": "font-family:monospace"
                },
                {
                    "name": "ircmd",
                    "title": "Command",
                    "type": "number",
                    "visible":false
                },
                {
                    "name": "ircmdtxt",
                    "title": "Command",
                    "type": "text",
                }
                ],
                rows: vals,
                editing: {
                    showText: "<span class=\"fooicon fooicon-pencil\" aria-hidden=\"true\"></span> Edit Commands",
                    addText: "New Command",
                    addRow: function () {
                        $editor[0].reset();
                        $editorTitle.text("Add a new Command");
                        $('#ircmdtxt').children().remove();
	                      IRCOMMANDS.forEach((cmd, index) =>
	                      {
	                        $('#ircmdtxt').append($("<option>").attr('value',index).text(cmd));  
                        });
                        $modal.modal("show");
                    },
                    editRow: function (row) {
                        var values = row.val();
                        $('#ircmdtxt').children().remove();
	                      IRCOMMANDS.forEach((cmd, index) =>
	                      {
	                        $('#ircmdtxt').append($("<option>").attr('value',index).text(cmd));  
                        });
                        $editor.find("#descr").val(values.descr);
                        $editor.find("#ircode").val(values.ircode);
                        $editor.find("#ircodetxt").val(values.ircodetxt);
                        $editor.find("#ircmdtxt").val(values.ircmd);
                        $editor.find("#ircmd").val(values.ircmd);
                        $modal.data("row", row);
                        $editorTitle.text("Edit Command # " + values.descr);
                        $modal.modal("show");
                        limitTable(ft, irmax);
                    },
                    deleteRow: function (row) {
                        var descr = row.value.descr;
                        if (confirm("This will remove command \""  + descr + "\" from database. Are you sure?")) {
                            var jsontosend = "{\"descr\":\"" + descr + "\",\"command\":\"remove\"}";
                            sendMessage(jsontosend);
                            row.delete();
                            limitTable(ft, irmax);
                        }
                    }
                },
            });
            ft.pageSize(8);
        $editor.on("submit", function (e) {
            if (this.checkValidity && !this.checkValidity()) {
                return;
            }
            e.preventDefault();
            var row = $modal.data("row"),
                values = {
                    descr: $editor.find("#descr").val(),
                    ircode: $editor.find("#ircode").val(),
                    ircodetxt: $editor.find("#ircodetxt").val(),
                    ircmd: $editor.find("#ircmdtxt").val(),
                    ircmdtxt: IRCOMMANDS[parseInt($editor.find("#ircmdtxt").val())],
                };
            var val = $editor.find("#ircode").val();
            if (row instanceof window.FooTable.Row) {
              if ((row.val().ircode==val) || !isNotUnique2(ft, val))
              {
                if (val != "")
                {
                  row.delete();
                  ft.rows.add(values);
                }
                else
                {
                  alert("The \"IR Code\" field must not be left blank !\nPlease press the appropriate button on the remote control !");
                  return; // stay in the editor!
                }
	            }
	            else
	            {
                alert("The command with the IR code \"" + parseInt(val).toString(16).toUpperCase().padStart(6, 0) + "\" already exists in the table!");
                return; // stay in the editor!
	            }
            }
            else
            {
              if (isNotUnique2(ft, val))
              {
              alert("The command with the IR code \"" + parseInt(val).toString(16).toUpperCase().padStart(6, 0) + "\" already exists in the table!");
              return; // stay in the editor!
              }
              if (val=="")
              {
              alert("The \"IR Code\" field must not be left blank !\nPlease press the appropriate button on the remote control !");
              return; // stay in the editor!
              }
            ft.rows.add(values);
            }
            $modal.modal("hide");
            limitTable(ft, irmax);
            var sorting = ft.use(FooTable.Sorting);
            sorting._sort(0, 0);
        });
        limitTable(ft, irmax);
    });
}



function readNetwTable() {
    var netwarr = [];
    var ft = FooTable.get("#netwtable");
    $.each(ft.rows.all, function (i, row) {
        v = row.val();
        values = {
            location: v.location,
            wifibssid: v.wifibssid,
            wifipass: v.wifipass,
            ssid: v.ssid,
            dhcp: v.dhcp,
            ipaddress: v.ipaddress,
            subnet: v.subnet,
            dnsadd: v.dnsadd,
            gateway: v.gateway
        };
        netwarr.push(values);
    });
    return netwarr;
}

function initNetwTable(vals) {
    jQuery(function ($) {
        var $modal = $("#editor-modal2"),
            $editor = $("#editor2"),
            $editorTitle = $("#editor-title2"),
            ft = window.FooTable.init("#netwtable", {
                columns: [
                {
                    "name": "location",
                    "title": "Location",
                    "type": "text"
                },
                {
                    "name": "ssid",
                    "title": "SSID",
                    "type": "text"
                },
                {
                    "name": "wifibssid",
                    "title": "BSSID",
                    "type": "text"
                },
                {
                    "name": "wifipass",
                    "title": "Password",
                    "type": "text",
                    "visible": false
                },
                {
                    "name": "dhcp",
                    "title": "DHCP",
                    "type": "number"
                },
                {
                    "name": "ipaddress",
                    "title": "IP Address",
                    "type": "text",
                    "visible": false
                },
                {
                    "name": "subnet",
                    "title": "Mask",
                    "type": "text",
                    "visible": false
                },
                {
                    "name": "dnsadd",
                    "title": "DNS",
                    "type": "text",
                    "visible": false
                },
                {
                    "name": "gateway",
                    "title": "Gateway",
                    "type": "text",
                    "visible": false
                },
                ],
                rows: vals,
                editing: {
                    showText: "<span class=\"fooicon fooicon-pencil\" aria-hidden=\"true\"></span> Edit Networks",
                    addText: "New Network",
                    addRow: function () {                                                
                        $editor[0].reset();
                        $editor.find("#staticip1").css('display','none');           //reset visibility
                        $editor.find("#staticip2").css('display','none');           //reset visibility
                        $editor.find("#inputtohide").css('display','block');        //reset visibility
                        $editor.find("#ssid").css('display', 'none');               //reset visibility
                        $editorTitle.text("Add a new Network");
                        $editor.find("#scanb").css('display', 'block');             //enable Scan button
                        $editor.find("#scanb").html("Scan");                        //reset text
                        $modal.modal("show");
                    },
                    editRow: function (row) {
                        $editor.find("#inputtohide").css('display','block');            //reset visibility
                        $editor.find("#ssid").css('display', 'none');                   //reset visibility
                        var values = row.val();
                        $editor.find("#location").val(values.location);
                        $editor.find("#inputtohide").val(values.ssid);
                        $editor.find("#wifibssid").val(values.wifibssid);
                        $editor.find("#wifipass").val(values.wifipass);
	                      $editor.find("#hideBSSID").css('display', 'block');
	                      $editor.find("#scanb").css('display', 'block');
	                      $editor.find("#dhcp").css('display', 'block');
                        handleDHCP(values);
                        $modal.data("row", row);
                        $editorTitle.text("Edit Network # " + values.location);
                        $modal.modal("show");
                    },
                    deleteRow: function (row) {
                        var loc = row.value.location;
                        if (confirm("This will remove network \"" + loc + "\" from database. Are you sure?")) {
                            var jsontosend = "{\"location\":\"" + loc + "\",\"command\":\"remove\"}";
                            sendMessage(jsontosend);
                            row.delete();
                            limitTable(ft, netwmax);
                        }
                    }
                },
            });
        $editor.on("submit", function (e) {
	if (parseInt(document.querySelector("input[name=\"dhcpenabled\"]:checked").value) === 0) {
		var dhcp = 0;
		if (!checkOctects("ipaddress")) {
			return;
		}
		if (!checkOctects("subnet")) {
			return;
		}
		if (!checkOctects("dnsadd")) {
			return;
		}
		if (!checkOctects("gateway")) {
			return;
		}
		var ipaddr = $editor.find("#ipaddress").val();
		var subn = $editor.find("#subnet").val();
		var dnsadd = $editor.find("#dnsadd").val();
		var gatw = $editor.find("#gateway").val();
	} else {
		var dhcp = 1;
		var ipaddr = "";
		var subn = "";
		var dnsadd = "";
		var gatw = "";
	}
      var inp = $('#inputtohide');
    	if (!inp.is(':visible')) 
    	{
	    	var b = document.getElementById("ssid");
		    var ssid = b.options[b.selectedIndex].value
		    inp.prop('required',false);
	    }
	    else 
	    {
		    var ssid = document.getElementById("inputtohide").value;
		    inp.prop('required',true);
	    }  
            if (this.checkValidity && !this.checkValidity()) {
                return;
            }
            var row = $modal.data("row");
 
            e.preventDefault();
                values = {
                    location: $editor.find("#location").val(),
                    wifibssid: $editor.find("#wifibssid").val(),
                    wifipass: $editor.find("#wifipass").val(),
                    ssid: ssid,
                    dhcp: dhcp,
                    ipaddress: ipaddr,
                    subnet: subn,
                    dnsadd: dnsadd,
                    gateway: gatw
                };
            if (row instanceof window.FooTable.Row) {
                row.delete();
                ft.rows.add(values);
            } else {
                ft.rows.add(values);
            }
            $modal.modal("hide");
            limitTable(ft, netwmax);
            var sorting = ft.use(FooTable.Sorting);
            sorting._sort(0, 0);        });
        limitTable(ft, netwmax);
    });
}


function listnetworkXX(netw) {
	document.getElementById("inputtohide").value = netw.ssid;
	document.getElementById("wifipass").value = netw.pswd;
	document.getElementById("wifibssid").value = netw.bssid;
	document.getElementById("hideBSSID").style.display = "block";
	document.getElementById("scanb").style.display = "block";
	document.getElementById("dhcp").style.display = "block";
	if (netw.dhcp === 1) {
		$("input[name=\"dhcpenabled\"][value=\"1\"]").prop("checked", true);
	}
	handleDHCP(netw);
}

function handleLowBatt()
{
  var enable = parseInt(	$(':radio[name="lowbatt"]:checked').val());
  if (enable)
  {
		$("#percentage").slideDown();
		$("#percentage").show();
  }
  else
  {
    $("#percentage").slideUp();
  }
}

function listgeneral() {
	document.getElementById("hostname").value = config.general.hostnm;
	if (AUTOSHUTDOWN)
	{
	  $("#asd2").css('display','block');
	  $('#dasd').attr('min', MINPWOFF);
    $('#dasd').attr('max', MAXPWOFF);
	}
	else
	{
	  $("#asd2").css('display','none');
	}
	if (BATTERY)
	{
	  $("#bat").css('display','block');
	  $('#bat0').val(config.general.bat0);
	  $('#bat100').val(config.general.bat100);
    $('#lowbatt'+config.general.lowbatt.toString()).prop('checked', true);
	  $('#critbatt').val(config.general.critbatt);
    $('#critbatt1').html(config.general.critbatt.toString());
    handleLowBatt();
	}
	else
	{
	  $("#bat").css('display','none');
	}
	document.getElementById("dasd").value = config.general.dasd.toString();
	document.getElementById("dasd1").innerText = config.general.dasd.toString();
}


function handleClock() {
  if (DISP)
  {
		$("#backlight0").slideDown();
		$("#backlight0").show();
  }
	if (document.getElementById("tmode3").checked) {
	  
		$("#idlerow").slideDown();
		$("#idlerow").show();
		if (DISP)
		{
		  $("#backlight").slideDown();
		  $("#backlight").show();
		}
		$("#calendar").slideDown();
		$("#calendar").show();
		if (document.getElementById("calon").checked) {
		  const date = new Date();
		    $("#dateformat option").each(function(i){
        $(this).text(formatDate(date, $(this).val())+"\xa0\xa0\xa0\xa0\xa0("+$(this).val()+")");
        });  
			document.getElementById("dateformat").value = config.display.dateformat;
			document.getElementById("day0").value = config.display.monday;
			document.getElementById("day1").value = config.display.tuesday;
			document.getElementById("day2").value = config.display.wednesday;
			document.getElementById("day3").value = config.display.thursday;
			document.getElementById("day4").value = config.display.friday;
			document.getElementById("day5").value = config.display.saturday;
			document.getElementById("day6").value = config.display.sunday;
			$("#formatdate").slideDown();
			$("#formatdate").show();
			$("#monday").slideDown();
			$("#monday").show();
			$("#tuesday").slideDown();
			$("#tuesday").show();
			$("#wednesday").slideDown();
			$("#wednesday").show();
			$("#thursday").slideDown();
			$("#thursday").show();
			$("#friday").slideDown();
			$("#friday").show();
			$("#saturday").slideDown();
			$("#saturday").show();
			$("#sunday").slideDown();
			$("#sunday").show();
		} else {
			$("#formatdate").slideUp();
			$("#monday").slideUp();
			$("#tuesday").slideUp();
			$("#wednesday").slideUp();
			$("#thursday").slideUp();
			$("#friday").slideUp();
			$("#saturday").slideUp();
			$("#sunday").slideUp();
		}
	} 
	else if (document.getElementById("tmode2").checked){
		$("#idlerow").slideDown();
		$("#idlerow").show();
		if (DISP)
		{
		  $("#backlight").slideDown();
		  $("#backlight").show();
		}
		$("#calendar").slideUp();
		$("#formatdate").slideUp();
		$("#monday").slideUp();
		$("#tuesday").slideUp();
		$("#wednesday").slideUp();
		$("#thursday").slideUp();
		$("#friday").slideUp();
		$("#saturday").slideUp();
		$("#sunday").slideUp();		
		
	}
	else if (document.getElementById("tmode1").checked){
		$("#idlerow").slideDown();
		$("#idlerow").show();
		$("#backlight").slideUp();
		$("#calendar").slideUp();
		$("#formatdate").slideUp();
		$("#monday").slideUp();
		$("#tuesday").slideUp();
		$("#wednesday").slideUp();
		$("#thursday").slideUp();
		$("#friday").slideUp();
		$("#saturday").slideUp();
		$("#sunday").slideUp();
	}
	else {
		$("#idlerow").slideUp();
		$("#backlight").slideUp();
		$("#calendar").slideUp();
		$("#formatdate").slideUp();
		$("#monday").slideUp();
		$("#tuesday").slideUp();
		$("#wednesday").slideUp();
		$("#thursday").slideUp();
		$("#friday").slideUp();
		$("#saturday").slideUp();
		$("#sunday").slideUp();
	}
}



function listdisplay() {
	if (DISP)
	{
	document.getElementById("backlight1").value = config.display.backlight1.toString();
	document.getElementById("rangeval11").innerText = config.display.backlight1.toString();
	document.getElementById("backlight2").value = config.display.backlight2.toString();
	document.getElementById("rangeval12").innerText = config.display.backlight2.toString();
	}
	document.getElementById("backlight").style.display = "none";
	document.getElementById("backlight0").style.display = "none";
	
	document.getElementById("speed").value = config.display.speed.toString();
	document.getElementById("rangeval3").innerText = config.display.speed.toString();
	document.getElementById("scrollgap").value = config.display.scrollgap.toString();
	document.getElementById("rangeval4").innerText = config.display.scrollgap.toString();
	document.getElementById("idle").value = config.display.idle.toString();
	document.getElementById("rangeval5").innerText = config.display.idle.toString();
	document.getElementById("calendar").style.display = "none";
	document.getElementById("formatdate").style.display = "none";
	document.getElementById("idlerow").style.display = "none";
	document.getElementById("monday").style.display = "none";
	document.getElementById("tuesday").style.display = "none";
	document.getElementById("wednesday").style.display = "none";
	document.getElementById("thursday").style.display = "none";
	document.getElementById("friday").style.display = "none";
	document.getElementById("saturday").style.display = "none";
	document.getElementById("sunday").style.display = "none";
	if (config.display.calendar === 1) {
		document.getElementById("calon").checked = true;
	} else {
		document.getElementById("calon").checked = false;
	}
	$('#sdclockon'+config.display.sdclock.toString()).prop('checked', true);
  $(':radio[name="timeoutmode"][value='+config.display.tmode+']').prop('checked', true);
	handleClock();
}

function updateMetadata(source)
{
  if (PM_RADIO)
  {
    var stl = $('#stationlist');
    if (stl.is(':visible')) 
    {
      if ($('#stationlist').find('option[value="'+station +'"]').length > 0)
      {
        $('#stlist select option[value="' + station + '"]').prop('selected', true);
        $('#testname').val('');
        $('#streamtotry').val('');
      }
      else
      {
        $('#stationlist').val('');
        $('#testname').val(station);
      }
    }
  }//PM_RADIO
  else
  {
    $('#album').val(album);
  }
  $('#artist').val(artist);
  $('#title').val(title);
}

function listBSSID() {
	var select = document.getElementById("ssid");
	document.getElementById("wifibssid").value = select.options[select.selectedIndex].bssidvalue;
}

function listSSID(obj) {
	var select = document.getElementById("ssid");
	$("#ssid").children().remove();
	for (var i = 0; i < obj.list.length; i++) {
		var x = parseInt(obj.list[i].rssi);
		var percentage = Math.min(Math.max(2 * (x + 100), 0), 100);
		var opt = document.createElement("option");
		opt.value = obj.list[i].ssid;
		opt.bssidvalue = obj.list[i].bssid;
		opt.innerHTML = obj.list[i].ssid + "   (Signal Strength: " + percentage + "% )";
		select.appendChild(opt);
	}
	document.getElementById("scanb").innerHTML = "Re-Scan";
	listBSSID();
}

function scanWifi() {
	sendMessage("{\"command\":\"scan\"}");
	document.getElementById("scanb").innerHTML = "...";
	document.getElementById("inputtohide").style.display = "none";
	var node = document.getElementById("ssid");
	node.style.display = "inline";
	while (node.hasChildNodes()) {
		node.removeChild(node.lastChild);
	}
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
	document.getElementById("spiffs").innerHTML = ajaxobj.availspiffs + " Bytes";
	document.getElementById("spiffs").style.width = (ajaxobj.availspiffs * 100) / ajaxobj.spiffssize + "%";
	colorStatusbar(document.getElementById("spiffs"));
	if (BATTERY)
	{
	$("#batprogress").css('display','table-row');
	document.getElementById("battery").innerHTML = ajaxobj.battery + " %";
  document.getElementById("battery").style.width = ajaxobj.battery + "%";
	colorStatusbar(document.getElementById("battery"));
	}
	else
	{
	  $("#batprogress").css('display','none');
	}
	document.getElementById("ssidstat").innerHTML = ajaxobj.ssid;
	document.getElementById("ip").innerHTML = ajaxobj.ip;
	document.getElementById("gate").innerHTML = ajaxobj.gateway;
	document.getElementById("mask").innerHTML = ajaxobj.netmask;
	document.getElementById("dns").innerHTML = ajaxobj.dns;
	document.getElementById("mac").innerHTML = ajaxobj.mac;
	document.getElementById("sver").innerText = version;
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
				case "#ntpcontent":
					listntp();
					break;
				case "#generalcontent":
					listgeneral();
					break;
				case "#controlcontent":
					break;
				case "#radiocontent":
					listradio();
					break;
				case "#sdcontent":
					listsd();
					break;
				case "#displaycontent":
					listdisplay();
					break;
				case "#hardwarecontent":
					listhardware();
					break;
				case "#irremotecontent":
					listirremote();
					break;
				case "#networkcontent":
					listnetwork();
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
			alert("You did not select file to restore!");
		} else {
			reader.onload = function() {
				var json;
				try {
					json = JSON.parse(reader.result);
				} catch (e) {
					alert("Not a valid backup file!");
					return;
				}
				if (json.command === "configfile") {
					var x = confirm("File seems to be valid, do you wish to continue?");
					if (x) {
						config = json;
						uncommited();
					}
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
      console.log("No JSON: ",evt.data)
        return; // error in the above string (in this case, yes)!
    }
	if (obj.hasOwnProperty("command")) {
	  //console.log("\ncommand: ",obj.command);
		switch (obj.command) {
      case "sdstatus":
        handleSDstate(obj);
        break;
			case "status":
				ajaxobj = obj;
				console.log(JSON.stringify(obj)); //keep permanently!
				getContent("#statuscontent");
				listStats();
				break;
			case "gettime":
				utcSeconds = obj.epoch;
				tzoffset = obj.tzoffset;
				deviceTime();
				break;
			case "ssidlist":
				listSSID(obj);
				break;
			case "station":
				break;
			case "ircode":
				var ircodetxt = document.getElementById("ircodetxt");
    	  if (isVisible(ircodetxt))
      	{
      	  document.getElementById("ircode").value = obj.ircode;
      	  ircodetxt.value = obj.ircode.toString(16).toUpperCase().padStart(6, 0);
      	}
				break;
			case "radio":
				PM_RADIO = true;
			    $("#tstsd").css('display','none'); 
			    $("#tstrs").css('display','block'); 
				station = obj.station;
				if (rowsnum==3)
				{
				  artist = obj.artist;
				  title = obj.title;
				}
				else
				{
				  var separator = obj.artist.indexOf(" - ");
				  if (separator > 0)
				  {
				    artist = obj.artist.substring(0, separator);
				    title = obj.artist.substring(separator+3);
				  }
				}
				if (rowsnum>0)
				{
          $("#disp").css('display','block');
				}
				muteval = obj.mute;
				volumeval = obj.volume;
			  var tstst = document.getElementById("tstst");
    	  if (!isVisible(tstst))
      	{
    	  var modalflg = true;
    	  var titlelist = document.getElementsByClassName("modal-title");
    	  for (var i = 0; i < titlelist.length; i++)
    	  {
    	     if (isVisible(titlelist[i]))
    	     {
    	       modalflg = false;
    	       break;
    	     }
    	  }
    	  if (modalflg && !isVisible(document.getElementById("stationlist")))
      	{
    	  getContent("#controlcontent");
      	}
        $('.collapse').collapse('hide');
        $("li.active").removeClass("active");
        $("#control").parent().addClass("active");
				listcontrol(obj.running);
				}
				break;
			case "sdplayer":
			  random = obj.random;
        loop = obj.loop;
			  $("#tstrs").css('display','none'); 
			  $("#tstsd").css('display','block'); 
			  SDREADY = obj.sdready;
			  if (!SDREADY)
			  {
			    TRACKS=null;
			  }
			  else
			  {
			    TRIX=obj.index;
			  }
      
        switch (obj.kind)
        {
          case "all":
				    album = obj.album;
				    artist = obj.artist;
				    title = obj.title;
				    break;
          case "album":
				    album = obj.album;
				    break;
          case "artist":
				    artist = obj.artist;
				    break;
          case "title":
				    title = obj.title;
				    break;
			    default:
				    break;
        }

				if (rowsnum>0)
				{
          $("#disp").css('display','block');
				}
				muteval = obj.mute;
				volumeval = obj.volume;
			  var tstst = document.getElementById("tstst");
    	  if (!isVisible(tstst))
      	{
    	  var modalflg = true;
    	  var titlelist = document.getElementsByClassName("modal-title");
    	  for (var i = 0; i < titlelist.length; i++)
    	  {
    	     if (isVisible(titlelist[i]))
    	     {
    	       modalflg = false;
    	       break;
    	     }
    	  }
    	  if (modalflg && !isVisible(document.getElementById("stationlist")))
      	{
      	  getContent("#controlcontent");
      	}
        $('.collapse').collapse('hide');
        $("li.active").removeClass("active");
        $("#sdcontrol").parent().addClass("active");
        if (PM_RADIO)
        {
          PM_RADIO = false;
        }
        listcontrol(obj.running);
				}
				break;
			case "mute":
				mute(obj.mute ? 1 : 0, 0);
				break;
			case "rndloop":
			  random = obj.random;
        loop = obj.loop;
				handlerndloop();
				break;
			case "volume":
				volume(obj.volume, 0);
				break;
			case "webconfig":
			    version = obj.version;
			    $("#mainver").text(version);
  			  if (obj.hasOwnProperty("ircommands"))
  			  {
  			    IRCOMMANDS=obj.ircommands;
  			  }
  			  if (obj.hasOwnProperty("reservedgpios"))
  			  {
  			    RESERVEDGPIOS=obj.reservedgpios;
  			  }
  			  if (obj.hasOwnProperty("rowsnum"))
  			  {
  			    rowsnum=obj.rowsnum;
  			  }
  			  if (obj.hasOwnProperty("disp"))
  			  {
            DISP = obj.disp;
  			  }				
  			  if (obj.hasOwnProperty("battery"))
  			  {
            BATTERY = obj.battery;
  			  }				
  			  if (obj.hasOwnProperty("autoshutdown"))
  			  {
            AUTOSHUTDOWN = obj.autoshutdown;
  			  }				
  			  if (obj.hasOwnProperty("minpwoff"))
  			  {
            MINPWOFF = obj.minpwoff;
  			  }				
  			  if (obj.hasOwnProperty("maxpwoff"))
  			  {
            MAXPWOFF = obj.maxpwoff;
  			  }				
  			  if (obj.hasOwnProperty("pwofftime"))
  			  {
            pwofftime = obj.pwofftime;
  			  }				
  			  if (obj.hasOwnProperty("ota"))
  			  {
            if (obj.ota)
            {
              $("#update_").css('display','block');
            }
  			  }				
  			  if (obj.hasOwnProperty("sdcard"))
  			  {
            SDCARD = obj.sdcard;
            if (SDCARD)
            {
              $("#sdctrl").css('display','block');
              $("#sdcfg").css('display','block');
              $("#sdclock").css('display','block');
            }
            else
            {
              $("#sdctrl").css('display','none');
              $("#sdcfg").css('display','none');
              $("#sdclock").css('display','none');
            }
            SDREADY = obj.sdready;
  			  }				
				break;
			case "asd":
        var pwoffminutes = obj.pwoffminutes;
        var pwofft = obj.pwofftime;
        if (pwofft>0)
        {
          pwofftime = pwofft;
          var asdx = document.getElementById("asd3");
          if (isVisible(asdx))
          {
            $("#asd3").css('display','none');
            $("#asd5").css('display','block');    
            id=setInterval(myTimer,1000,id);
            $("#schedpwoff").prop('disabled', true);
            $("#cancpwoff").prop('disabled', false);
          }
        }
        else
        {
          pwofftime = 0;
          clearInterval(id);
          var asdtxt = document.getElementById("asd5");
          if (isVisible(asdtxt))
          {
            $("#asd5").css('display','none');
            $("#asd3").css('display','block');    
            $("#schedpwoff").prop('disabled', false);
            $("#cancpwoff").prop('disabled', true);
          }
        }
				break;
			case "configfile":
				config = obj;
				config.default=false;
				break;
			case "heartbeat":
			  // reset the counter for missed heartbeats
        missed_heartbeats = 0;
				break;
			case "sdtrack":
			  $("#tstrs").css('display','none'); 
			  $("#tstsd").css('display','block'); 
			  TRIX = obj.index;
			  album="";
			  artist="";
			  atitle="";
			  SDREADY = obj.sdready;
			  if (!SDREADY)
			  {
			    TRACKS=null;
			  }
				station = obj.station;
				if (rowsnum==3)
				{
				  artist = obj.artist;
				  title = obj.title;
				}
				else
				{
				  var separator = obj.artist.indexOf(" - ");
				  if (separator > 0)
				  {
				    artist = obj.artist.substring(0, separator);
				    title = obj.artist.substring(separator+3);
				  }
				}
				if (rowsnum>0)
				{
          $("#disp").css('display','block');
				}
				muteval = obj.mute;
				volumeval = obj.volume;
			  var tstst = document.getElementById("tstst");
    	  if (!isVisible(tstst))
      	{
    	  var modalflg = true;
    	  var titlelist = document.getElementsByClassName("modal-title");
    	  for (var i = 0; i < titlelist.length; i++)
    	  {
    	     if (isVisible(titlelist[i]))
    	     {
    	       modalflg = false;
    	       break;
    	     }
    	  }
    	  if (modalflg && !isVisible(document.getElementById("stationlist")))
      	{
      	  getContent("#controlcontent");
      	}
        $("li.active").removeClass("active");
        $("#sdcontrol").parent().addClass("active");
        if (PM_RADIO)
        {
          PM_RADIO = false;
        }
        listcontrol(obj.running);
				}
			  break
			case "sdready":
			  SDREADY = obj.sdready;
			  if (!SDREADY)
			  {
			    TRACKS=null;
			  }
			  else
			  {
          if (!TRACKS)
          {
            mp3list_req = true;
            $.get( //async !!!
            "/mp3list", {}, function(data)
            {
              TRACKS = data;
            });
          }			    
			  }
			  break;
      case "adcbat":
        handleadcbat(obj);
        break;
      case "loginpassw":
        doupdate(obj);
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

function compareDestroy() {
	if (config.general.hostnm === document.getElementById("compare").value) {
		$("#destroybtn").prop("disabled", false);
	} else {
		$("#destroybtn").prop("disabled", true);
	}
}

function destroy() {
	inProgress("destroy");
}

$("#dismiss, .overlay").on("click", function() {
	$("#sidebar").removeClass("active");
	$(".overlay").fadeOut();
});

$("#sidebarCollapse").on("click", function() {
	$("#sidebar").addClass("active");
	$(".overlay").fadeIn();
	$(".collapse.in").toggleClass("in");
	$("a[aria-expanded=true]").attr("aria-expanded", "false"); //mozna prop misto attr ?
});

$("#status").click(function() {
  $('.collapse').collapse('hide');
  $("li.active").removeClass("active");
  $("#status").parent().addClass("active");
	sendMessage("{\"command\":\"status\"}");
	return false;
});

$("#dropdown").click(function() {
  $("li.active").removeClass("active");
	return true;
});

$("#network").on("click", (function() {
	getContent("#networkcontent");
	return false;
}));

$("#hardware").click(function() {
  sendMessage("{\"command\":\"hw\"}");
	getContent("#hardwarecontent");
	return false;
});

$("#irremote").click(function() {
	getContent("#irremotecontent");
	return false;
});

$("#general").click(function() {
	getContent("#generalcontent");
	return false;
});

$("#control").click(function() {
  $("li.active").removeClass("active");
  $("#control").parent().addClass("active");
  sendMessage("{\"command\":\"radio\"}");
	return false;
});

$("#sdcontrol").click(function() {
  $("li.active").removeClass("active");
  $("#sdcontrol").parent().addClass("active");
  sendMessage("{\"command\":\"sdplayer\"}");
	return false;
});

$("#radio").click(function() {
	getContent("#radiocontent");
	return false;
});

$("#sd").click(function() {
	getContent("#sdcontent");
	return false;
});

$("#display").click(function() {
	getContent("#displaycontent");
	return false;
});

$("#ntp").click(function() {
	getContent("#ntpcontent");
	return false;
});

$("#backup").click(function() {
  $('.collapse').collapse('hide');
  $("li.active").removeClass("active");
  $("#backup").parent().addClass("active");
	getContent("#backupcontent");
	return false;
});

$("#reset").click(function() {
	$("#destroy").modal("show");
	return false;
});

$(".noimp").on("click", function() {
	$("#noimp").modal("show");
});

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
     sendMessage("{\"command\":\"pmode\"}"); 
    });
};

	websock.onclose = function(evt) {
	  websock = null;
	  gotInitialData = false;
	  heartbeat_interval = null
	  document.location = "index.html";
	};
}

function doupdate(o)
{
  var valid = o.valid
  if (valid)
  {
	  formData.append("bin", $("#binform")[0].files[0]);
	  inProgress("upload");
  }
  else
  {
    alert("Username or password do not match !");
  }
}

function upload() {
  PASSW = btoa($('#updpassw').val());
  LOGIN = $('#updlogin').val();
  sendMessage("{\"command\":\"loginpassw\",\"login\":\""+LOGIN+"\",\"passw\":\""+PASSW+"\"}"); 
}

function getLatestReleaseInfo() {
	$("#versionhead").val(version);
	$("#downloadupdate").prop("disabled", true);
	updurl = "";
	$.getJSON("https://api.github.com/repos/Pako2/radioesp32/releases/latest").done(function(release) {
		var asset = release.assets[0];
		updurl = asset.browser_download_url;
		var downloadCount = 0;
		for (var i = 0; i < release.assets.length; i++) {
			downloadCount += release.assets[i].download_count;
		}
		var oneHour = 60 * 60 * 1000;
		var oneDay = 24 * oneHour;
		var dateDiff = new Date() - new Date(release.published_at);
		var timeAgo;
		if (dateDiff < oneDay) {
			timeAgo = (dateDiff / oneHour).toFixed(1) + " hours ago";
		} else {
			timeAgo = (dateDiff / oneDay).toFixed(1) + " days ago";
		}
    if (updurl)
    {
      $("#downloadupdate").prop("disabled", false);
    }
		var releaseInfo = release.name + " was updated " + timeAgo + " and downloaded " + downloadCount.toLocaleString() + " times.";
		$("#releasehead").text(releaseInfo);
		$("#releasebody").text(release.body);
		$("#releaseinfo").fadeIn("slow");
	}).error(function() {
		$("#onlineupdate").html("<h5>Couldn't get release info. Are you connected to the Internet?</h5>");
	});
}

function downloadupdate()
{
  if (updurl)
  {
  fnix = updurl.lastIndexOf("/") + 1,
  filename = updurl.substr(fnix);
  $('#update').modal('hide');
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

$("#update").on("shown.bs.modal", function(e) {
	getLatestReleaseInfo();
});

function allowUpload() {
  var len1 = $('#binform').val().length;
  var len2 = $('#updlogin').val().length;
  var len3 = $('#updpassw').val().length;
  if ( len1 == 0 || len2 == 0 || len3 == 0 )
  {
	  $("#upbtn").prop("disabled", true);
    return;
  }
	$("#upbtn").prop("disabled", false);
}

function start() {
	radioesp32 = document.createElement("div");
	radioesp32.id = "mastercontent";
	radioesp32.style.display = "none";
	document.body.appendChild(radioesp32);
  connectWS();
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

document.addEventListener("touchstart", handleTouchStart, false);
document.addEventListener("touchmove", handleTouchMove, false);