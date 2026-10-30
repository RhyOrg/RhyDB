import { expect } from 'chai';
import { server } from './common.js';
import { describe, it } from 'node:test';

// The e2e instance is started with RHYDB_API_CORS_ALLOWED_ORIGINS=https://allowed.example
// (see docker-compose-for-tests-api.yml).
const ALLOWED_ORIGIN = 'https://allowed.example';
const FOREIGN_ORIGIN = 'https://evil.example';

describe('CORS', () => {
  it('adds CORS headers for an allowed origin', async () => {
    await server
      .post('/query')
      .set('Origin', ALLOWED_ORIGIN)
      .set('Content-Type', 'text/plain')
      .send('data.group(by:={}, aggs:={count:=count()})')
      .expect(200)
      .expect('Access-Control-Allow-Origin', ALLOWED_ORIGIN)
      .expect('Vary', /Origin/)
      .expect(response => {
        expect(response.headers).to.have.property('access-control-expose-headers');
      });
  });

  it('answers a preflight from an allowed origin with 204', async () => {
    await server
      .options('/query')
      .set('Origin', ALLOWED_ORIGIN)
      .set('Access-Control-Request-Method', 'POST')
      .set('Access-Control-Request-Headers', 'content-type')
      .expect(204)
      .expect('Access-Control-Allow-Origin', ALLOWED_ORIGIN)
      .expect('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
      .expect('Access-Control-Allow-Headers', 'content-type');
  });

  it('rejects a preflight from a foreign origin with 403', async () => {
    await server
      .options('/query')
      .set('Origin', FOREIGN_ORIGIN)
      .set('Access-Control-Request-Method', 'POST')
      .expect(403)
      .expect(response => {
        expect(response.headers).to.not.have.property('access-control-allow-origin');
      });
  });

  it('does not add an Allow-Origin header for a foreign origin', async () => {
    await server
      .post('/query')
      .set('Origin', FOREIGN_ORIGIN)
      .set('Content-Type', 'text/plain')
      .send('data.group(by:={}, aggs:={count:=count()})')
      .expect(200)
      .expect(response => {
        expect(response.headers).to.not.have.property('access-control-allow-origin');
      });
  });

  it('does not add CORS headers when no Origin is sent', async () => {
    await server
      .post('/query')
      .set('Content-Type', 'text/plain')
      .send('data.group(by:={}, aggs:={count:=count()})')
      .expect(200)
      .expect(response => {
        expect(response.headers).to.not.have.property('access-control-allow-origin');
      });
  });
});
