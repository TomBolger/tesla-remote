"""Fake Tesla cloud + car for testing. Reads one HTTP request as JSON on stdin, prints JSON response.
The car side uses Tesla's real protobuf schema and the `cryptography` library, independent of the JS."""
import sys, json, os, base64, hashlib, hmac, struct, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'pb'))
import universal_message_pb2 as um, signatures_pb2 as sg, car_server_pb2 as cs, vcsec_pb2 as vc
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives import serialization

STATE = os.path.join(os.path.dirname(__file__), 'state.json')
VIN = '5YJ30123456789ABC'
V_PRIV = ec.derive_private_key(0x344EE5B466A7CF1EEB12B6F50331DB2E5EC5834EF5F4BEFCFD8CBE55C2528D70, ec.SECP256R1())
V_PUB = V_PRIV.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)

def load():
    if os.path.exists(STATE):
        return json.load(open(STATE))
    return {'asleep': True, 'epoch': os.urandom(16).hex(), 'counter': {}, 'boot': time.time(), 'log': [], 'paired': None}
st = load()
def save(): json.dump(st, open(STATE, 'w'))
def clock(): return int(time.time() - st['boot']) + 1000

def tlv(tag, b): return bytes([tag, len(b)]) + b
def K_for(pub):
    peer = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), pub)
    return hashlib.sha1(V_PRIV.exchange(ec.ECDH(), peer)).digest()[:16]

def handle_routable(raw):
    req = um.RoutableMessage(); req.ParseFromString(raw)
    dom = req.to_destination.domain
    resp = um.RoutableMessage()
    resp.from_destination.domain = dom
    resp.to_destination.routing_address = req.from_destination.routing_address
    resp.request_uuid = req.uuid
    if req.HasField('session_info_request'):
        pub = req.session_info_request.public_key
        info = sg.SessionInfo(counter=st['counter'].get(str(dom), 0), publicKey=V_PUB,
                              epoch=bytes.fromhex(st['epoch']), clock_time=clock())
        if st['paired'] is not None and st['paired'] != pub.hex():
            info.status = sg.SESSION_INFO_STATUS_KEY_NOT_ON_WHITELIST
        enc = info.SerializeToString()
        K = K_for(pub)
        key = hmac.new(K, b'session info', hashlib.sha256).digest()
        meta = tlv(0, bytes([sg.SIGNATURE_TYPE_HMAC])) + tlv(2, VIN.encode()) + tlv(6, req.uuid) + b'\xff'
        resp.session_info = enc
        resp.signature_data.session_info_tag.tag = hmac.new(key, meta + enc, hashlib.sha256).digest()
        st['log'].append({'domain': dom, 'type': 'handshake'})
        return resp.SerializeToString()
    sd = req.signature_data
    h = sd.HMAC_Personalized_data
    K = K_for(sd.signer_identity.public_key)
    meta = (tlv(0, bytes([sg.SIGNATURE_TYPE_HMAC_PERSONALIZED])) + tlv(1, bytes([dom])) + tlv(2, VIN.encode()) +
            tlv(3, h.epoch) + tlv(4, struct.pack('>I', h.expires_at)) + tlv(5, struct.pack('>I', h.counter)))
    if req.flags: meta += tlv(7, struct.pack('>I', req.flags))
    meta += b'\xff'
    ck = hmac.new(K, b'authenticated command', hashlib.sha256).digest()
    good = hmac.compare_digest(hmac.new(ck, meta + req.protobuf_message_as_bytes, hashlib.sha256).digest(), h.tag)
    last = st['counter'].get(str(dom), 0)
    fault = 0
    if not good: fault = um.MESSAGEFAULT_ERROR_INVALID_SIGNATURE
    elif h.epoch.hex() != st['epoch']: fault = um.MESSAGEFAULT_ERROR_INCORRECT_EPOCH
    elif h.counter <= last: fault = um.MESSAGEFAULT_ERROR_INVALID_TOKEN_OR_COUNTER
    elif not (clock() <= h.expires_at <= clock() + 60): fault = um.MESSAGEFAULT_ERROR_TIME_EXPIRED
    entry = {'domain': dom, 'hmac_ok': good, 'counter': h.counter, 'expires_in': h.expires_at - clock(), 'flags': req.flags, 'fault': fault}
    if fault:
        resp.signedMessageStatus.operation_status = um.OPERATIONSTATUS_ERROR
        resp.signedMessageStatus.signed_message_fault = fault
        st['log'].append(entry); return resp.SerializeToString()
    st['counter'][str(dom)] = h.counter
    if dom == um.DOMAIN_INFOTAINMENT:
        a = cs.Action(); a.ParseFromString(req.protobuf_message_as_bytes)
        entry['cmd'] = str(a).replace('\n', ' ').strip()
        out = cs.Response()
        if a.vehicleAction.HasField('vehicleControlHonkHornAction'):
            out.actionStatus.result = cs.OPERATIONSTATUS_ERROR
            out.actionStatus.result_reason.plain_text = 'user_not_present'
        plain = out.SerializeToString()
    else:
        u = vc.UnsignedMessage(); u.ParseFromString(req.protobuf_message_as_bytes)
        entry['cmd'] = 'which=' + str(u.WhichOneof('sub_message')) + ' ' + str(u).replace('\n', ' ').strip()
        plain = vc.FromVCSECMessage().SerializeToString()
    nonce = os.urandom(12)
    ct = AESGCM(K).encrypt(nonce, plain, b'')  # tag not checked by client; AD irrelevant here
    resp.protobuf_message_as_bytes = ct[:-16]
    resp.signature_data.AES_GCM_Response_data.nonce = nonce
    resp.signature_data.AES_GCM_Response_data.counter = 1
    resp.signature_data.AES_GCM_Response_data.tag = ct[-16:]
    st['log'].append(entry)
    return resp.SerializeToString()

def main():
    r = json.load(sys.stdin)
    url, method, body = r['url'], r['method'], r.get('body') or ''
    def out(status, obj): print(json.dumps({'status': status, 'text': json.dumps(obj)}))
    if url.endswith('/oauth2/v3/token'):
        st['log'].append({'token': body.split('&')[0]})
        save(); return out(200, {'access_token': 'AT', 'refresh_token': 'RT', 'expires_in': 28800})
    path = url.split('.com', 1)[1]
    if path == '/api/1/users/region': save(); return out(200, {'response': {'region': 'na', 'fleet_api_base_url': 'https://fleet-api.prd.na.vn.cloud.tesla.com'}})
    if path == '/api/1/partner_accounts': st['log'].append({'register': body}); save(); return out(200, {'response': {}})
    if path == '/api/1/vehicles': save(); return out(200, {'response': [{'vin': VIN, 'display_name': 'Blue Thunder', 'state': 'asleep'}]})
    if path == '/api/1/vehicles/' + VIN: save(); return out(200, {'response': {'state': 'asleep' if st['asleep'] else 'online'}})
    if path.endswith('/wake_up'): st['asleep'] = False; st['log'].append({'wake': 1}); save(); return out(200, {'response': {'state': 'online'}})
    if st['asleep']: save(); return out(408, {'response': None, 'error': 'vehicle unavailable: vehicle is offline or asleep'})
    if path.endswith('/signed_command'):
        raw = base64.b64decode(json.loads(body)['routable_message'])
        res = handle_routable(raw); save()
        return out(200, {'response': base64.b64encode(res).decode()})
    if path.endswith('/nearby_charging_sites'):
        save(); return out(200, {'response': {'superchargers': [{'name': 'Spokane Valley, WA', 'distance_miles': 3.2, 'available_stalls': 6, 'total_stalls': 12}],
                                               'destination_charging': [{'name': 'Library Lot', 'distance_miles': 0.4}]}})
    if '/vehicle_data' in path:
        save(); return out(200, {'response': {'display_name': 'Blue Thunder', 'state': 'online',
            'charge_state': {'battery_level': 78, 'battery_range': 241.3, 'charging_state': 'Charging', 'charge_limit_soc': 80, 'charger_power': 7, 'minutes_to_full_charge': 85, 'charge_current_request': 32, 'charge_current_request_max': 48, 'charge_port_door_open': True, 'charge_energy_added': 12.4, 'charger_actual_current': 32},
            'climate_state': {'inside_temp': 18.5, 'outside_temp': 9.0, 'driver_temp_setting': 21.0, 'is_climate_on': False,
                              'is_front_defroster_on': False, 'is_rear_defroster_on': False, 'seat_heater_left': 0, 'seat_heater_right': 0,
                              'seat_heater_rear_left': 0, 'seat_heater_rear_center': 0, 'seat_heater_rear_right': 0,
                              'climate_keeper_mode': 'off', 'cabin_overheat_protection': 'FanOnly', 'cop_activation_temperature': 'High',
                              'auto_seat_climate_left': True, 'auto_seat_climate_right': False},
            'vehicle_state': {'locked': True, 'ft': 0, 'rt': 0, 'vehicle_name': 'Blue Thunder', 'sentry_mode': False, 'odometer': 12345.6,
                              'tpms_pressure_fl': 2.9, 'tpms_pressure_fr': 2.9, 'tpms_pressure_rl': 2.85, 'tpms_pressure_rr': 2.9,
                              'car_version': '2026.38.4 abc', 'software_update': {'status': 'available', 'version': '2026.44'},
                              'df': 0, 'pf': 0, 'dr': 0, 'pr': 1, 'fd_window': 0, 'fp_window': 0, 'rd_window': 0, 'rp_window': 0, 'is_user_present': False},
            'vehicle_config': {'car_type': 'model3'}, 'gui_settings': {'gui_temperature_units': 'F', 'gui_distance_units': 'mi/hr'}}})
    save(); out(404, {'error': 'not found ' + path})
main()
