/* app.js — vanilla JS front-end for the libigrf localhost demo. */
"use strict";

function $(id) {
    return document.getElementById(id);
}

/* Decimal year conversion with the same convention as
 * igrf_datetime_to_decimal_year(): year + (doy - 1 + day fraction)
 * / days_in_year (366 for leap years). */
function decimalYearFromDatetime(value) {
    const d = new Date(value);
    if (isNaN(d.getTime())) return NaN;
    const year = d.getFullYear();
    const start = new Date(year, 0, 1);
    const daysInYear = isLeap(year) ? 366 : 365;
    const dayOfYear = Math.floor((d - start) / 86400000); // 0-based
    const dayFraction =
        (d.getHours() * 3600 + d.getMinutes() * 60 + d.getSeconds()) / 86400 +
        d.getMilliseconds() / 86400000;
    return year + (dayOfYear + dayFraction) / daysInYear;
}

function isLeap(y) {
    return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
}

function currentCoordType() {
    return document.querySelector('input[name="coord"]:checked').value;
}

function populateModels(models) {
    const select = $("model");
    select.innerHTML = "";
    for (const name of models) {
        const opt = document.createElement("option");
        opt.value = name;
        opt.textContent = name + ".shc";
        select.appendChild(opt);
    }
    if (models.includes("igrf14")) select.value = "igrf14";
}

async function calculate(ev) {
    ev.preventDefault();
    const params = new URLSearchParams();
    params.set("model", $("model").value);
    params.set("year", $("year").value);
    params.set("coord", currentCoordType());

    if (currentCoordType() === "geodetic") {
        params.set("lon", $("lon").value);
        params.set("lat", $("lat").value);
        params.set("height", $("height").value);
    } else {
        params.set("r", $("r").value);
        params.set("theta", $("theta").value);
        params.set("phi", $("phi").value);
    }

    const result = $("result");
    const error = $("error");
    error.textContent = "";
    result.hidden = false;

    try {
        const resp = await fetch("/api/compute?" + params.toString());
        const data = await resp.json();
        if (!data.ok) {
            $("meta").textContent = "";
            $("geodetic-result").hidden = true;
            $("geocentric-result").hidden = true;
            error.textContent = "Error: " + data.message;
            return;
        }
        if (data.geodetic) {
            $("meta").textContent =
                data.model + ".shc, decimal year " + data.year +
                ", lon " + data.lon + "°, lat " + data.lat + "°, height " +
                data.height + " km (geodetic)";
            $("X").textContent = fmt(data.X);
            $("Y").textContent = fmt(data.Y);
            $("Z").textContent = fmt(data.Z);
            $("H").textContent = fmt(data.H);
            $("F").textContent = fmt(data.F);
            $("D").textContent = fmt(data.D);
            $("I").textContent = fmt(data.I);
            $("geodetic-result").hidden = false;
            $("geocentric-result").hidden = true;
        } else {
            $("meta").textContent =
                data.model + ".shc, decimal year " + data.year +
                ", r " + data.r + " km, θ " + data.theta + "°, φ " +
                data.phi + "° (geocentric)";
            $("Br").textContent = fmt(data.Br);
            $("Btheta").textContent = fmt(data.Btheta);
            $("Bphi").textContent = fmt(data.Bphi);
            $("Fgc").textContent = fmt(data.F);
            $("geodetic-result").hidden = true;
            $("geocentric-result").hidden = false;
        }
    } catch (err) {
        error.textContent = "Request failed: " + err;
    }
}

function fmt(x) {
    return Number(x).toLocaleString("en-US", {
        minimumFractionDigits: 2,
        maximumFractionDigits: 2
    });
}

function syncFieldVisibility() {
    const geodetic = currentCoordType() === "geodetic";
    $("geodetic-fields").hidden = !geodetic;
    $("geocentric-fields").hidden = geodetic;
}

async function init() {
    document.querySelectorAll('input[name="coord"]').forEach((el) => {
        el.addEventListener("change", syncFieldVisibility);
    });
    syncFieldVisibility();

    $("form").addEventListener("submit", calculate);
    $("convert").addEventListener("click", () => {
        const v = $("date").value;
        if (!v) return;
        const dec = decimalYearFromDatetime(v);
        if (isFinite(dec)) $("year").value = dec.toFixed(7);
    });

    try {
        const resp = await fetch("/api/models");
        const data = await resp.json();
        if (data.ok) {
            populateModels(data.models);
            if (data.version) {
                $("version").textContent = "v" + data.version;
            }
        }
    } catch (err) {
        /* server unreachable: the form is unusable anyway */
    }
}

init();
