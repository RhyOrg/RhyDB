import { describe, it } from 'node:test';
import { expect } from 'chai';
import { server } from './common.js';

const WRITE_QUERY = 'data.insertInto(data)';

async function countRows() {
  const response = await server
    .post('/query')
    .set('Content-Type', 'text/plain')
    .send('data.group(by:={}, aggs:={count:=count()})');

  expect(response.status).to.equal(200);
  const rows = response.text
    .split(/\n/)
    .filter(line => line !== '')
    .map(line => JSON.parse(line));
  return rows[0].count;
}

describe('Write statements', () => {
  // The read-only endpoint must refuse a write statement instead of executing it.
  it('should be rejected by the /query endpoint without touching the data', async () => {
    const rowsBefore = await countRows();

    await server
      .post('/query')
      .set('Content-Type', 'text/plain')
      .send(WRITE_QUERY)
      .expect(400)
      .expect('Content-Type', 'application/json')
      .expect({
        error: 'Bad request',
        message:
          "this query writes to the database; use the 'POST /admin/query' endpoint for write statements",
      });

    expect(await countRows()).to.equal(rowsBefore);
  });

  // The e2e instance runs without a runtime config, so `api.allowAdminEndpoint` keeps its default
  // value `false` and the write-enabled endpoint is not served at all.
  it('should not reach /admin/query while api.allowAdminEndpoint is disabled', async () => {
    const rowsBefore = await countRows();

    await server
      .post('/admin/query')
      .set('Content-Type', 'text/plain')
      .send(WRITE_QUERY)
      .expect(404)
      .expect('Content-Type', 'application/json')
      .expect({ error: 'Not found', message: 'Resource /admin/query does not exist' });

    expect(await countRows()).to.equal(rowsBefore);
  });
});
