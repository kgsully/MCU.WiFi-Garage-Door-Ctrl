var connection = new WebSocket('ws://'+location.hostname+':8081/');
        
        var door1_CMD = 0;
        var door1_Light = 0;
        var door2_CMD = 0;
        var door2_Light = 0;

        connection.onopen = function(evt) {
            var pageLoadTrigger = '{"LoadTrigger" : 1}';
            connection.send(pageLoadTrigger);
            console.log("Connected!");
        }
        connection.onmessage = function(event) {
            var feedback_data = event.data;
            console.log(feedback_data);
            var data = JSON.parse(feedback_data);

            // Determine Door 1 Status
            if (data.door1_ZSO == 0 && data.door1_ZSC == 0) {                           // Door In Transition
                document.getElementById("door1Status").textContent="TRANSITION \u00A0";
                document.getElementById("door1Status").style.color = 'yellow';
            } else if (data.door1_ZSO == 1 && data.door1_ZSC == 0) {                    // Door Open
                document.getElementById("door1Status").textContent="OPEN \u00A0";
                document.getElementById("door1Status").style.color = 'red';
            } else if (data.door1_ZSO == 0 && data.door1_ZSC == 1) {                    // Door Closed
                document.getElementById("door1Status").textContent="CLOSED \u00A0";
                document.getElementById("door1Status").style.color = 'green';
            } else {
                document.getElementById("door1Status").textContent="--- FAULT --- \u00A0";
                document.getElementById("door1Status").style.color = 'red';
            } 

            if (data.door1_LockSts == 1) {
                document.getElementById('door1_LockIco').src = "locked";
            } else {
                document.getElementById('door1_LockIco').src = "unlocked";
            }

            
            // Determine Door 2 Status
            if (data.door2_ZSO == 0 && data.door2_ZSC == 0) {                           // Door In Transition
                document.getElementById("door2Status").textContent="TRANSITION \u00A0";
                document.getElementById("door2Status").style.color = 'yellow';
            } else if (data.door2_ZSO == 1 && data.door2_ZSC == 0) {                    // Door Open
                document.getElementById("door2Status").textContent="OPEN \u00A0";
                document.getElementById("door2Status").style.color = 'red';
            } else if (data.door2_ZSO == 0 && data.door2_ZSC == 1) {                    // Door Closed
                document.getElementById("door2Status").textContent="CLOSED \u00A0";
                document.getElementById("door2Status").style.color = 'green';
            } else {
                document.getElementById("door2Status").textContent="--- FAULT --- \u00A0";
                document.getElementById("door2Status").style.color = 'red';
            }

            if (data.door2_LockSts == 1) {
                document.getElementById('door2_LockIco').src = "locked";
            } else {
                document.getElementById('door2_LockIco').src = "unlocked";
            }
        }

        function door1Cmd() {
	        door1_CMD = 1;
	        console.log("Door 1 Command");
	        send_data();
            door1_CMD = 0;
        }
        function door1Light() {
	        door1_Light = 1;
	        console.log("Door 1 Light Command");
	        send_data();
            door1_Light = 0;
        }
        function door2Cmd() {
	        door2_CMD = 1;
	        console.log("Door 2 Command");
	        send_data();
            door2_CMD = 0;
        }
        function door2Light() {
	        door2_Light = 1;
	        console.log("Door 2 Light Command");
	        send_data();
            door2_Light = 0;
        }

        function send_data() {
	        var data_payload = '{"door1_CMD" : '+door1_CMD+', "door1_Light" : '+door1_Light+', "door2_CMD" : '+door2_CMD+',  "door2_Light" : '+door2_Light+'}';
            console.log(data_payload);
	        connection.send(data_payload);
        }