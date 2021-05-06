static const char PROGMEM TELEMETRY_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang=en>
    <head>
        <meta charset="UTF-8">
        <meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 5.0">
        <title>F1 2020 Telemetry Debug</title>
        <style type="text/css">
            table {
                position: absolute;
                top: 0;
                bottom: 0;
                left: 0;
                right: 0;
                height: 100%;
                width: 100%;
                border-collapse: collapse;
            }
            td {
                border: 1px solid;
                font-size: 200%;
            }
        </style>
        <script type="text/javascript">
            var websock;
            var connected = false;

            function message_from_server(evt) {
                console.log(evt);
                var obj = JSON.parse(evt.data);
                if (obj.type === 1) {
                    document.getElementById("gear").innerHTML = obj.gear;
                    document.getElementById("speed").innerHTML = obj.speed;
                    document.getElementById("rpm").innerHTML = obj.rpm;
                    document.getElementById("drs").innerHTML = obj.drs;
                    document.getElementById("revLights").innerHTML = obj.revLights;
                    document.getElementById("engineTemp").innerHTML = obj.engineTemp;
                }
                else if (obj.type === 2) {
                    document.getElementById("fuel_mix").innerHTML = obj.fuel_mix;
                    document.getElementById("front_brake_bias").innerHTML = obj.front_brake_bias;
                    document.getElementById("fuel_remaining_laps").innerHTML = obj.fuel_remaining_laps;
                }
                else if (obj.type === 3) {
                    document.getElementById("ers_store_energy").innerHTML = obj.ers_store_energy;
                    document.getElementById("ers_deploy_mode").innerHTML = obj.ers_deploy_mode;
                    document.getElementById("ers_deployed_this_lap").innerHTML = obj.ers_deployed_this_lap;
                }
                else if (obj.type === 4) {
                    document.getElementById("brakes").innerHTML = obj.brakes;
                    document.getElementById("tyres_surface").innerHTML = obj.tyres_surface;
                    document.getElementById("tyres_inner").innerHTML = obj.tyres_inner;
                }
                else if (obj.type === 5) {
                    document.getElementById("diff_on_throttle").innerHTML = obj.diff_on_throttle;
                    document.getElementById("diff_off_throttle").innerHTML = obj.diff_off_throttle;
                }
                else if (obj.type === 6) {
                    document.getElementById("orientation").innerHTML = obj.orientation;
                }
            };

            function start() {
                // sending a connect request to the server.
                websock = new WebSocket('ws://' + window.location.hostname + ':81/');
                websock.onopen = function(evt) {
                    console.log('websock onopen', evt);
                    connected = true;
                    var e = document.getElementById('webSockStatus');
                    e.style.backgroundColor = 'green';
                    e.style.color = 'white';
                };
                websock.onclose = function(evt) {
                    console.log('websock onclose', evt);
                    connected = false;
                    var e = document.getElementById('webSockStatus');
                    e.style.backgroundColor = 'red';
                    e.style.color = 'white';
                };
                websock.onerror = function(evt) {
                    console.log('websock onerror', evt);
                };
                websock.onmessage = message_from_server;
            }
        </script>
    </head>
    <body onload="javascript:start();">
        <table id="my_table" frame="border">
            <tbody>
                <tr>
                    <td style="text-align:right"><button id="webSockStatus" type="button" onclick="window.location.reload();">Connect</button>
                        <button type="button" onclick="openFullscreen();">Fullscreen Mode</button>
                    </td>
                    <td></td>
                </tr>
                <tr>
                    <td style="text-align:right">Gear</td>
                    <td id="gear" style="text-align:left">N</td>
                </tr>
                <tr>
                    <td style="text-align:right">Speed, KPH</td>
                    <td id="speed" style="text-align:left">0</td>
                </tr>
                <tr>
                    <td style="text-align:right">Engine RPM</td>
                    <td id="rpm" style="text-align:left">0</td>
                </tr>
                <tr>
                    <td style="text-align:right">DRS</td>
                    <td id="drs" style="text-align:left">0</td>
                </tr>
                <tr>
                    <td style="text-align:right">Rev Lights %</td>
                    <td id="revLights" style="text-align:left">0</td>
                </tr>
                <tr>
                    <td style="text-align:right">Engine, &deg;C</td>
                    <td id="engineTemp" style="text-align:left">0</td>
                </tr>
                <tr>
                    <td style="text-align:right">Fuel Mix</td>
                    <td id="fuel_mix" style="text-align:left">Standard</td>
                    <!-- <td style="text-align:left"><button type="button">Up</button><button type="button">Down</button></td> -->
                </tr>
                <tr>
                    <td style="text-align:right">Front Brake Bias</td>
                    <td id="front_brake_bias" style="text-align:left">50</td>
                    <!-- <td style="text-align:left"><button type="button">Up</button><button type="button">Down</button></td> -->
                </tr>
                <tr>
                    <td style="text-align:right">Fuel Remaining, Laps</td>
                    <td id="fuel_remaining_laps" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">Roll, Pitch, Yaw, radians</td>
                    <td id="orientation" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">ERS Energy</td>
                    <td id="ers_store_energy" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">ERS Deploy Mode</td>
                    <td id="ers_deploy_mode" style="text-align:left"></td>
                    <!-- <td style="text-align:left"><button type="button">Up</button><button type="button">Down</button></td> -->
                </tr>
                <tr>
                    <td style="text-align:right">ERS Deployed This Lap</td>
                    <td id="ers_deployed_this_lap" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">Brakes, &deg;C</td>
                    <td id="brakes" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">Tyres Surface, &deg;C</td>
                    <td id="tyres_surface" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">Tyres Inner, &deg;C</td>
                    <td id="tyres_inner" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">Differential on throttle</td>
                    <td id="diff_on_throttle" style="text-align:left"></td>
                </tr>
                <tr>
                    <td style="text-align:right">Differential off throttle</td>
                    <td id="diff_off_throttle" style="text-align:left"></td>
                </tr>
            </tbody>
        </table>
        <script type="text/javascript">
            const whole_page = document.documentElement;
            function openFullscreen() {
                if (whole_page.requestFullscreen) {
                    whole_page.requestFullscreen();
                } else if (whole_page.webkitRequestFullscreen) { /* Safari */
                    whole_page.webkitRequestFullscreen();
                } else if (whole_page.msRequestFullscreen) { /* IE11 */
                    whole_page.msRequestFullscreen();
                }
            }

            function closeFullscreen() {
                if (document.exitFullscreen) {
                    document.exitFullscreen();
                } else if (document.webkitExitFullscreen) { /* Safari */
                    document.webkitExitFullscreen();
                } else if (document.msExitFullscreen) { /* IE11 */
                    document.msExitFullscreen();
                }
            }
        </script>
    </body>
</html>
)rawliteral";
