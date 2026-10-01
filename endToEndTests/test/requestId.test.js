import { expect } from 'chai';
import { server } from './common.js';
import { describe, it } from 'node:test';

const X_REQUEST_ID = 'x-request-id';

describe('The request id', () => {
  it('should be returned when explicitly specified', async () => {
    const requestID = 'hardcodedRequestIdInTheTest';

    await server
      .post('/query')
      .set(X_REQUEST_ID, requestID)
      .set('Content-Type', 'text/plain')
      .send('default.group(by:={}, aggs:={count:=count()})')
      .expect(200)
      .expect(X_REQUEST_ID, requestID);
  });

  it('should be generated when none is specified', async () => {
    await server
      .post('/query')
      .set('Content-Type', 'text/plain')
      .send('default.group(by:={}, aggs:={count:=count()})')
      .expect(200)
      .expect(response => {
        const headers = response.headers;
        expect(headers).to.have.property(X_REQUEST_ID);
        expect(headers[X_REQUEST_ID]).to.be.a('string');
        expect(headers[X_REQUEST_ID]).length.to.be.at.least(1);
      });
  });
});
