const currentModel = require('geomagnetism').model();;
let decl = null;
const Clay = require("@rebble/clay");
const Keys = require('message_keys');

const clayConfig = require("./config");
const clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var curr_lat = 0;
var curr_lon = 0;

var settings = {
	version: 0,
	target_locs: [{ lat: 0, lon: 0, label: "" }],
	curr_target_idx: 0,
}

function saveSettings() {
	localStorage.setItem("settings", JSON.stringify(settings));
}
function loadSettings() {
	try {
		const old_settings = JSON.parse(localStorage.getItem("settings")) || settings;
		if (old_settings.version === settings.version) {
			settings = old_settings;
		}
	}
	catch (e) { }
}
loadSettings();
saveSettings();

Pebble.addEventListener('showConfiguration', function(_e) {
	Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function(e) {
	if (e && !e.response) { return; }

	var dict = clay.getSettings(e.response);
	if (Keys.TargetAddress in dict) {
		const url = new URL("https://nominatim.openstreetmap.org/search");
		url.searchParams.set("q", dict[Keys.TargetAddress]);
		url.searchParams.set("limit", 1);
		url.searchParams.set("format", "jsonv2");
		// Add email because for some reason it gets blocked otherwise...?
		url.searchParams.set("email", "my@mail.com");
		xhrRequest(url, "GET", setTarget);
	}

	Pebble.sendAppMessage(dict, null, function(e) {
		console.warn("Failed to send config data!", e);
	});
});

// Calculate bearing based off https://www.igismap.com/formula-to-find-bearing-or-heading-angle-between-two-points-latitude-longitude/
function updateAngle() {
	const curr_target = { lat: settings.target_locs[settings.curr_target_idx].lat * Math.PI / 180, lon: settings.target_locs[settings.curr_target_idx].lon * Math.PI / 180 };
	const curr_loc = { lat: curr_lat * Math.PI / 180, lon: curr_lon * Math.PI / 180 };
	const a = curr_loc.lat;
	const b = curr_target.lat;
	const d_l = Math.abs(curr_target.lon - curr_loc.lon);
	const x = Math.cos(b) * Math.sin(d_l);
	const y = Math.cos(a) * Math.cos(b) - Math.sin(a) * Math.cos(b) * Math.cos(d_l);
	const bearing = Math.atan2(x, y) / Math.PI * 180;
	dict = { "CompassTargetAngle": Math.round(bearing) };
	Pebble.sendAppMessage(dict, null,
		function(e) {
			console.warn("Error sending compass declination!", e);
		}
	);
	console.log("from", curr_lat, curr_lon);
	console.log("to", settings.target_locs[0]);
	console.log("bearing", bearing);
}

function setTarget(res) {
	if (!res) {
		console.warn("Null response when getting coords from address!");
		return;
	}
	settings.target_locs[settings.curr_target_idx] = { lat: res[0].lat, lon: res[0].lon };
	saveSettings();
	updateAngle();
}


// Adapted from https://github.com/HarrisonAllen/SheikahSlate_PebbleWatchface/
function xhrRequest(url, type, callback) {
	const xhr = new XMLHttpRequest();
	xhr.addEventListener("load", () => callback(xhr.response));
	xhr.addEventListener("error", console.log);
	xhr.responseType = "json";
	xhr.open(type, url);
	xhr.setRequestHeader("User-Agent", navigator.userAgent);
	xhr.send();
};

function locationSuccess(pos) {
	console.log("Got location!");
	curr_lat = pos.coords.latitude;
	curr_lon = pos.coords.longitude;
	const info = currentModel.point([curr_lat, curr_lon])
	const new_decl = Math.floor(info.decl * 100);
	if (new_decl === decl) { return; }
	decl = new_decl;
	dict = { "CompassDecl": decl, "HasLocation": true };
	Pebble.sendAppMessage(dict, null,
		function(e) {
			console.warn("Error sending compass declination!", e);
		}
	);
	updateAngle();
}

function peekLocation() {
	navigator.geolocation.getCurrentPosition(
		locationSuccess,
		locationError,
		{
			enableHighAccuracy: false,
			timeout: 5000,
			maximumAge: 0,
		}
	)
}

function locationError(err) {
	console.warn("Error requesting location!", err);
	dict = { "HasLocation": false };
	Pebble.sendAppMessage(dict, null,
		function(e) {
			console.warn("Error sending compass declination!", e);
		}
	);
	// This only starts looking for location if location was off when the app first started up
	setTimeout(peekLocation, 15000);
}

navigator.geolocation.watchPosition(
	locationSuccess,
	locationError,
	{
		enableHighAccuracy: false,
		timeout: 5000,
		maximumAge: 0,
	}
);
peekLocation();
