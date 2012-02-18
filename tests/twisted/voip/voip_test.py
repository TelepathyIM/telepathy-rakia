
import dbus
import uuid

import twisted.protocols.sip

from servicetest import (
    make_channel_proxy,
    assertContains,
    )
import constants as cs

class VoipTestContext(object):
    # Default audio codecs for the remote end
    audio_codecs = [ ('GSM', 3, 8000, {}),
        ('PCMA', 8, 8000, {}),
        ('PCMU', 0, 8000, {}) ]

    # Default video codecs for the remote end. I have no idea what's
    # a suitable value here...
    video_codecs = [ ('H264', 96, 90000, {}) ]

    # Default candidates for the remote end
    remote_candidates = [
        (1, # Component
         "192.168.0.1", # ip
         2222, # port
         {'protocol': cs.MEDIA_STREAM_BASE_PROTO_UDP,
          'priority': 0}),
        (2, # Component
         "192.168.0.1", # ip
         2223, # port
         {'protocol': cs.MEDIA_STREAM_BASE_PROTO_UDP,
          'priority': 0})
        ]

    _mline_template = 'm=%(mediatype)s %(port)s RTP/AVP %(codec_ids)s'
    _aline_template = 'a=rtpmap:%(codec_id)s %(name)s/%(rate)s'

    def __init__(self, q, conn, bus, sip_proxy, our_uri, peer):
        self.bus = bus
        self.conn = conn
        self.q = q
        self.our_uri = our_uri
        self.peer = peer
        self.peer_id = "sip:" + peer
        self.sip_proxy = sip_proxy
        self._cseq_id = 1
        self.to = None
      
    def dbusify_codecs(self, codecs):
        dbussed_codecs = [ (id, name, rate, 1, False, params )
                            for (name, id, rate, params) in codecs ]
        return dbus.Array(dbussed_codecs, signature='(usuuba{ss})')

    def dbusify_codecs_with_params (self, codecs):
        return self.dbusify_codecs(codecs)

    def get_md_dbus(self, codecs, remote_contact):
        return dbus.Dictionary(
            {cs.CALL_CONTENT_MEDIA_DESCRIPTION + ".Codecs": self.dbusify_codecs(codecs),
             cs.CALL_CONTENT_MEDIA_DESCRIPTION + ".RemoteContact":
                 dbus.UInt32(remote_contact)},
            signature='sv')

    def get_audio_md_dbus(self, remote_contact):
        return self.get_md_dbus(self.audio_codecs, remote_contact)

    def get_video_md_dbus(self, remote_contact):
        return self.get_md_dbus(self.video_codecs, remote_contact)

    def get_remote_candidates_dbus(self):
        return dbus.Array(self.remote_candidates, signature='(usua{sv})')
        
    def get_call_sdp(self, medias):
        (component, ip, port, info) = self.remote_candidates[0]
        codec_id_list = []
        codec_list = []
        for name, codec_id, rate, _misc in self.audio_codecs:
            codec_list.append('a=rtpmap:%(codec_id)s %(name)s/%(rate)s' % locals()) 
            codec_id_list.append(str(codec_id))
        codec_ids = ' '.join(codec_id_list)
        codecs = '\r\n'.join(codec_list)

        sdp_string = 'v=0\r\n' + \
            'o=- 7047265765596858314 2813734028456100815 IN IP4 %(ip)s\r\n' + \
            's=-\r\n' + \
            't=0 0\r\n'
        for m in medias:
            sdp_string += 'm=' + m[0] + ' %(port)s RTP/AVP 3 8 0\r\n' \
                'c=IN IP4 %(ip)s\r\n' \
                '%(codecs)s\r\n'
            if m[1]:
                sdp_string += 'a=' + m[1] + '\r\n'

        return sdp_string % locals()

    def check_call_sdp(self, sdp_string, medias=[('audio',None)]):
        codec_id_list = []
        for name, codec_id, rate, _misc in self.audio_codecs:
            assertContains (self._aline_template % locals(), sdp_string)
            codec_id_list.append(str(codec_id))
        codec_ids = ' '.join(codec_id_list)

        (component, ip, port, info) = self.remote_candidates[0]
        for m in medias:
            mediatype = m[0]
            assert self._mline_template % locals() in sdp_string
        
    def send_message(self, message_type, body='', to_=None, from_=None, 
                     **additional_headers):
        url = twisted.protocols.sip.parseURL('sip:testacc@127.0.0.1')
        msg = twisted.protocols.sip.Request(message_type, url)
        if body:
            msg.body = body
            msg.addHeader('content-length', '%d' % len(msg.body))
        msg.addHeader('from', from_ or '<%s>;tag=XYZ' % self.peer_id)
        msg.addHeader('to', to_ or self.to or '<sip:testacc@127.0.0.1>')
        self._cseq_id += 1
        additional_headers.setdefault('cseq', '%d %s' % (self._cseq_id, message_type))
        for key, vals in additional_headers.items():
            if not isinstance(vals, list):
                vals = [vals]
            k = key.replace('_', '-')
            for v in vals:
                msg.addHeader(k, v)
        via = self.sip_proxy.getVia()
        via.branch = 'z9hG4bKXYZ'
        msg.addHeader('via', via.toString())
        _expire, destination = self.sip_proxy.registry.users['testacc']
        self.sip_proxy.sendMessage(destination, msg)
        return msg
    
    def accept(self, invite_message, body=None):
        self.call_id = invite_message.headers['call-id'][0]
        if invite_message.headers['from'][0].find('tag='):
            self.to = invite_message.headers['from'][0]
        response = self.sip_proxy.responseFromRequest(200, invite_message)
        # Echo rakia's SDP back to it. It doesn't care.
        response.addHeader('content-type', 'application/sdp')
        response.body = body or invite_message.body
        response.addHeader('content-length', '%d' % len(response.body))
        self.sip_proxy.deliverResponse(response)
        return response

    def pr_respond(self, invite_message, number):
        self.call_id = invite_message.headers['call-id'][0]
        response = self.sip_proxy.responseFromRequest(number, invite_message)
        self.sip_proxy.deliverResponse(response)
        return response
    
    def ack(self, ok_message):
        cseq = '%s ACK' % ok_message.headers['cseq'][0].split()[0]
        self.send_message('ACK', call_id=self.call_id, cseq=cseq)
    
    def reinvite(self, medias=[('audio',None)]):
        body = self.get_call_sdp(medias)
        return self.send_message('INVITE', body, content_type='application/sdp',
                   supported='timer, 100rel', call_id=self.call_id)
        
    def incoming_call(self, medias=[('audio',None)]):
        self.call_id = uuid.uuid4().hex
        body = self.get_call_sdp(medias)
        return self.send_message('INVITE', body, content_type='application/sdp',
                   supported='timer, 100rel', call_id=self.call_id)
        
    def incoming_call_from_self(self):
        self.call_id = uuid.uuid4().hex
        body = self.get_call_sdp([('audio',None)])
        return self.send_message('INVITE', body, content_type='application/sdp',
                   supported='timer, 100rel', call_id=self.call_id, 
                   from_='<sip:testacc@127.0.0.1>')
        
    def terminate(self):
        return self.send_message('BYE', call_id=self.call_id)

    def options_ping(self, q):
        self.send_message('OPTIONS',
                          supported='timer, 100rel', call_id=self.call_id)
        acc = q.expect('sip-response', call_id=self.call_id, code=200,
                        cseq='%s OPTIONS' % (self._cseq_id))
        self.ack(acc.sip_message)
