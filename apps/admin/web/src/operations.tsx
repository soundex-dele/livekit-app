import {useEffect, useRef, useState} from 'react';
import {Alert, App, Button, Card, Descriptions, Drawer, Form, Input, Modal, Select, Space, Table, Typography} from 'antd';
import {useQuery, useQueryClient} from '@tanstack/react-query';
import {useAdminScope} from './api';
import {canRetryMediaTask, formatTime, orgPath, pendingOperation, stateNames} from './domain';
import {emptyTable, NoOrganization, PageHeading, RequestError, StateTag} from './components';
import type {MediaOperation, Page} from './types';

export function OperationTable({organization, resource, operationIds}: {organization: string; resource?: string; operationIds?: string[]}) {
  const scope = useAdminScope();
  const [page, setPage] = useState(1);
  const operations = useQuery({queryKey: [...scope.key, 'resource-operations', organization, resource, page], enabled: !!organization,
    queryFn: ({signal}) => scope.client.get<Page<MediaOperation>>(orgPath(organization) + '/operations', {signal, query: {resource_id: resource, limit: 20, offset: (page - 1) * 20}}),
    refetchInterval: query => !query.state.data || query.state.data.items.some(row => pendingOperation(row.state)) ? 2000 : false, refetchIntervalInBackground: false});
  return <><Space className="av-section-toolbar"><Typography.Title level={4}>媒体操作进度</Typography.Title><Button size="small" onClick={() => void operations.refetch()}>刷新</Button></Space>
    {operations.error && <RequestError error={operations.error} retry={() => void operations.refetch()}/>}
    <OperationsGrid data={operations.data} loading={operations.isLoading} page={page} setPage={setPage} operationIds={operationIds}/>
  </>;
}

function OperationsGrid({data, loading, page, setPage, operationIds}: {data?: Page<MediaOperation>; loading: boolean; page: number; setPage: (page: number) => void; operationIds?: string[]}) {
  const scope = useAdminScope();
  const {message} = App.useApp();
  const cache = useQueryClient();
  const [selected, setSelected] = useState<MediaOperation | null>(null);
  const [retryTarget, setRetryTarget] = useState<MediaOperation | null>(null);
  const [retryKey, setRetryKey] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<unknown>();
  const alive = useRef(true);
  const currentScope = useRef(scope.identity);
  currentScope.current = scope.identity;
  useEffect(() => {alive.current = true; return () => {alive.current = false;};}, []);
  useEffect(() => {setSelected(null); setRetryTarget(null); setError(undefined);}, [scope.identity]);
  const canRetry = !!scope.actor && canRetryMediaTask(scope.actor.permissions);
  async function retry() {
    if (!retryTarget || !retryTarget.job_id) return;
    const identity = scope.identity;
    setBusy(true); setError(undefined);
    try {
      const path = scope.actor?.user.admin ? `/admin/organizations/${encodeURIComponent(retryTarget.organization_id)}/jobs/${encodeURIComponent(retryTarget.job_id)}/retry` : `/jobs/${encodeURIComponent(retryTarget.job_id)}/retry`;
      await scope.client.mutate('POST', path, {}, {key: retryKey});
      if (!alive.current || currentScope.current !== identity) return;
      setRetryTarget(null); message.success('已提交重试，请关注媒体操作进度。');
      await cache.invalidateQueries({queryKey: scope.key});
    } catch (cause) {if (alive.current && currentScope.current === identity) setError(cause);} finally {if (alive.current && currentScope.current === identity) setBusy(false);}
  }
  const columns = [
    {title: '操作', dataIndex: 'action', render: (value: string, row: MediaOperation) => <Button type="link" className="av-table-link" onClick={() => setSelected(row)}>{value}</Button>},
    {title: '状态', dataIndex: 'state', render: (value: string) => <StateTag state={value}/>},
    {title: '资源', dataIndex: 'resource_id', ellipsis: true},
    {title: '更新时间', dataIndex: 'updated', render: formatTime},
    {title: '处理', key: 'actions', render: (_: unknown, row: MediaOperation) => row.state === 'failed' && row.job_id && canRetry ? <Button size="small" onClick={() => {setRetryTarget(row); setRetryKey(crypto.randomUUID()); setError(undefined);}}>重试任务</Button> : '—'},
  ];
  return <>
    <Table rowKey="id" className="av-desktop-table" locale={emptyTable} loading={loading} dataSource={data?.items ?? []} columns={columns} rowClassName={row => operationIds?.includes(row.id) ? 'av-highlight-row' : ''} scroll={{x: 760}} pagination={{current: page, pageSize: 20, total: data?.total ?? 0, showSizeChanger: false, onChange: setPage}}/>
    <div className="av-mobile-cards">{data?.items.map(row => <Card size="small" key={row.id}><Space><Button type="link" onClick={() => setSelected(row)}>{row.action}</Button><StateTag state={row.state}/></Space><Typography.Paragraph className="av-id">{row.resource_id}</Typography.Paragraph><Typography.Text type="secondary">{formatTime(row.updated)}</Typography.Text>{row.state === 'failed' && row.job_id && canRetry && <Button onClick={() => {setRetryTarget(row); setRetryKey(crypto.randomUUID());}}>重试任务</Button>}</Card>)}<div className="av-pagination"><Button disabled={page <= 1} onClick={() => setPage(page - 1)}>上一页</Button><span>第 {page} 页</span><Button disabled={page * 20 >= (data?.total ?? 0)} onClick={() => setPage(page + 1)}>下一页</Button></div></div>
    <Drawer title="媒体操作详情" open={!!selected} onClose={() => setSelected(null)} width={540}>{selected && <Descriptions column={1} bordered size="small" items={[{key: 'id', label: '操作编号', children: selected.id}, {key: 'org', label: '组织', children: selected.organization_id}, {key: 'resource', label: '资源', children: selected.resource_id}, {key: 'state', label: '状态', children: <StateTag state={selected.state}/>}, {key: 'job', label: '关联任务', children: selected.job_id || '—'}, {key: 'created', label: '创建时间', children: formatTime(selected.created)}, {key: 'updated', label: '更新时间', children: formatTime(selected.updated)}]}/>}</Drawer>
    <Modal title="重试媒体任务" open={!!retryTarget} onCancel={() => !busy && setRetryTarget(null)} onOk={() => void retry()} confirmLoading={busy} okText="提交重试" cancelText="取消" destroyOnClose>
      <Alert type="warning" showIcon message="请先核对资源当前状态" description="重试会由任务队列重新执行媒体操作，并再次校验状态版本。"/>{!!error && <RequestError error={error}/>}<Typography.Paragraph className="av-id">任务：{retryTarget?.job_id}</Typography.Paragraph>
    </Modal>
  </>;
}

export function OperationsPage() {
  const scope = useAdminScope();
  const [state, setState] = useState('');
  const [resource, setResource] = useState('');
  const [input, setInput] = useState('');
  const [page, setPage] = useState(1);
  useEffect(() => {setPage(1); setState(''); setResource(''); setInput('');}, [scope.identity]);
  const data = useQuery({queryKey: [...scope.key, 'operations', state, resource, page], enabled: scope.allowed, queryFn: ({signal}) => scope.client.get<Page<MediaOperation>>(scope.path + '/operations', {signal, query: {state: state || undefined, resource_id: resource || undefined, limit: 20, offset: (page - 1) * 20}}), refetchInterval: query => !query.state.data || query.state.data.items.some(row => pendingOperation(row.state)) ? 2000 : 10000, refetchIntervalInBackground: false});
  if (!scope.allowed) return <NoOrganization/>;
  return <div className="av-page"><PageHeading title="媒体操作" description="跟踪业务操作后的媒体清理，查看排队、重试和失败状态。" extra={<Button onClick={() => void data.refetch()} loading={data.isFetching}>刷新</Button>}/>
    <Card><Form layout="inline" className="av-filters" onFinish={() => {setResource(input.trim()); setPage(1);}}><Form.Item label="状态"><Select aria-label="媒体操作状态" value={state} onChange={value => {setState(value); setPage(1);}} options={[{value: '', label: '全部状态'}, ...['queued', 'retrying', 'succeeded', 'superseded', 'failed'].map(value => ({value, label: stateNames[value]}))]} style={{width: 150}}/></Form.Item><Form.Item label="资源编号"><Input value={input} onChange={event => setInput(event.target.value)} placeholder="输入完整资源编号"/></Form.Item><Button htmlType="submit">查询</Button></Form></Card>
    {data.error && <RequestError error={data.error} retry={() => void data.refetch()}/>}
    <Card><OperationsGrid key={scope.identity} data={data.data} loading={data.isLoading} page={page} setPage={setPage}/></Card>
  </div>;
}
