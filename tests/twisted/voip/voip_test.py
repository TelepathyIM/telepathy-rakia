
import dbus

import twisted.protocols.sip

class VoipTestContext(object):
    # Default audio codecs for the remote end
    audio_codecs = [ ('GSM', 3, 8000, {}),
        ('PCMA', 8, 8000, {}),
        ('PCMU', 0, 8000, {}) ]

    # Default video codecs for the remote end. I have no idea what's
    # a suitable value here...
    video_codecs = [ ('WTF', 42, 80000, {}) ]

    # Default candidates for the remote end
    remote_transports = [
          ( "192.168.0.1", # host
            666, # port
            0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
            "RTP", # protocol subtype
            "AVP", # profile
            1.0, # preference
            0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
            "username",
            "password" ) ]

    _mline_template = 'm=audio %(port)s %(subtype)s/%(profile)s %(codec_ids)s'
    _aline_template = 'a=rtpmap:%(codec_id)s %(name)s/%(rate)s'

    def __init__(self, q, conn, bus, sip_proxy, our_uri, peer):
        self.bus = bus
        self.conn = conn
        self.q = q
        self.our_uri = our_uri
        self.peer = peer
        self.peer_id = "sip:" + peer
        self.sip_proxy = sip_proxy
      
    def dbusify_codecs(self, codecs):
        dbussed_codecs = [ (id, name, 0, rate, 0, params )
                            for (name, id, rate, params) in codecs ]
        return dbus.Array(dbussed_codecs, signature='(usuuua{ss})')

    def dbusify_codecs_with_params (self, codecs):
        return self.dbusify_codecs(codecs)

    def get_audio_codecs_dbus(self):
        return self.dbusify_codecs(self.audio_codecs)

    def get_video_codecs_dbus(self):
        return self.dbusify_codecs(self.video_codecs)

    def dbusify_call_codecs(self, codecs):
        dbussed_codecs = [ (id, name, rate, 0, params)
                            for (name, id, rate, params) in codecs ]
        return dbus.Array(dbussed_codecs, signature='(usuua{ss})')

    def dbusify_call_codecs_with_params(self, codecs):
        return dbusify_call_codecs (self, codecs)

    def get_call_audio_codecs_dbus(self):
        return self.dbusify_call_codecs(self.audio_codecs)

    def get_call_video_codecs_dbus(self):
        return self.dbusify_call_codecs(self.video_codecs)


    def get_remote_transports_dbus(self):
        return dbus.Array([
            (dbus.UInt32(1 + i), host, port, proto, subtype,
                profile, pref, transtype, user, pwd)
                for i, (host, port, proto, subtype, profile,
                    pref, transtype, user, pwd)
                in enumerate(self.remote_transports) ],
            signature='(usuussduss)')

    def get_call_remote_transports_dbus(self):
        return dbus.Array([
            (1 , host, port,
                { "Type": transtype,
                  "Foundation": "",
                  "Protocol": proto,
                  "Priority": int((1+i) * 65536),
                  "Username": user,
                  "Password": pwd }
             ) for i, (host, port, proto, subtype, profile,
                    pref, transtype, user, pwd)
                in enumerate(self.remote_transports) ],
            signature='(usqa{sv})')
        
    def get_call_sdp(self):
        (ip, port, protocol, subtype, profile, preference, 
                transport, username, password) = self.remote_transports[0]

        codec_id_list = []
        codec_list = []
        for name, codec_id, rate, _misc in self.audio_codecs:
            codec_list.append('a=rtpmap:%(codec_id)s %(name)s/%(rate)s' % locals()) 
            codec_id_list.append(str(codec_id))
        codec_ids = ' '.join(codec_id_list)
        codecs = '\r\n'.join(codec_list)

        sdp_string = ('v=0\r\n'
            'o=- 7047265765596858314 2813734028456100815 IN IP4 %(ip)s\r\n'
            's=-\r\n'
            't=0 0\r\n'
            'm=audio %(port)s RTP/AVP 3 8 0\r\n'
            'c=IN IP4 %(ip)s\r\n'
            '%(codecs)s\r\n') % locals()
        return sdp_string

    def check_call_sdp(self, sdp_string):
        codec_id_list = []
        for name, codec_id, rate, _misc in self.audio_codecs:
            assert self._aline_template % locals() in sdp_string
            codec_id_list.append(str(codec_id))
        codec_ids = ' '.join(codec_id_list)

        (ip, port, protocol, subtype, profile, preference, 
                transport, username, password) = self.remote_transports[0]
        assert self._mline_template % locals() in sdp_string
    
    def accept(self, invite_message):
        self.call_id = invite_message.headers['call-id'][0]
        response = self.sip_proxy.responseFromRequest(200, invite_message)
        # Echo sofiasip's SDP back to it. It doesn't care.
        response.addHeader('content-type', 'application/sdp')
        response.body = invite_message.body
        response.addHeader('content-length', '%d' % len(response.body))
        self.sip_proxy.deliverResponse(response)
        return response
