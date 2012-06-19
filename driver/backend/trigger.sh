frontend_id=$2
device_name=$1
if [ $# != 2 ]
then
        echo "Usage: $0 <device name> <frontend-id>"
				echo "Using defaults: device_name = omx, frontend-id = 1"
frontend_id=1
device_name=omx
fi

xenstore-chmod /local/domain/${frontend_id} b${frontend_id}
# Write backend information into the location the frontend will look
# for it.
xenstore-write /local/domain/${frontend_id}/device/${device_name}/0/backend-id 0
xenstore-write /local/domain/${frontend_id}/device/${device_name}/0/backend \
							 /local/domain/0/backend/${device_name}/${frontend_id}/0
# Write frontend information into the location the backend will look
# for it.
xenstore-write /local/domain/0/backend/${device_name}/${frontend_id}/0/frontend-id ${frontend_id}
xenstore-write /local/domain/0/backend/${device_name}/${frontend_id}/0/frontend \
							 /local/domain/${frontend_id}/device/${device_name}/0

#xenstore-chmod /local/domain/0/backend/${device_name} r${frontend_id}
#xenstore-chmod /local/domain/0/backend/${device_name}/${frontend_id} r${frontend_id}
xenstore-chmod /local/domain/0/backend/${device_name}/${frontend_id}/0 r${frontend_id}

# Write the states.  Note that the backend state must be written
# last because it requires a valid frontend state to already be
# written.
xenstore-write /local/domain/${frontend_id}/device/${device_name}/0/handle 1
xenstore-write /local/domain/${frontend_id}/device/${device_name}/0/mac "0a:11:22:33:44:5${frontend_id}"
xenstore-write /local/domain/${frontend_id}/device/${device_name}/0/state 1
xenstore-write /local/domain/0/backend/${device_name}/${frontend_id}/0/mac "0a:12:32:23:32:11"
xenstore-write /local/domain/0/backend/${device_name}/${frontend_id}/0/handle 1
xenstore-write /local/domain/0/backend/${device_name}/${frontend_id}/0/state 1
